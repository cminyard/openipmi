/*
 * A program to test the IPMI driver on Linux.
 *
 * This program will create a socket that will allow a user to execute
 * certain commands.  This is designed to be used with a test driver
 * that starts QEMU using the OpenIPMI library's ipmi_sim program to
 * simulate an external BMC.  This program runs inside QEMU to allow
 * the test driver to do what it needs.
 *
 * This program expect the IPMI driver to be compiled as modules and all
 * the modules to be in the current directory.  We don't use the kernel
 * built by yocto/distro, as we are testing a new kernel.
 *
 *   Load <id> msghandler|si|smbus|devintf
 *     Load a driver
 *   Unload <id> msghandler|si|smbus|devintf
 *     Unoad a driver
 *   Cycle <id> msghandler|si|smbus|devintf <count>
 *     Cycle loading/unloading the given driver as fast as possible
 *   Command <id> <dev> <addr> <netfn> <cmd> <data>
 *     Send a command
 *   Response <id> <dev> <addr> <netfn> <cmd> <data>
 *     Send a response
 *   Broadcast <id> <dev> <addr> <netfn> <cmd> <data>
 *     Send a broadcast
 *   Register <id> <dev> <netfn> <cmd> [<channels>]
 *     Register for command
 *   Unregister <id> <dev> <netfn> <cmd> [<channels>]
 *     Unregister for command
 *   EvEnable <id> <dev> <enable>
 *     Set event enable (1 or 0 for enable or disable)
 *   Open <id> <dev>
 *     Open IPMI device
 *   Close <id> <dev>
 *     Close IPMI device
 *   Panic <id>
 *     Panic the system to test the panic logs
 *   Quit <id>
 *     Shut down the program
 *
 * <dev> is the particular IPMI device, 0-9.
 *
 * <addr> is:
 *   si <channel> <lun> 
 *   ipmb <channel> <ipmb> <lun> 
 *   lan <channel> <privilege> <handle> <rSWID> <lSWID> <lun> 
 *
 * Asynchronous received data is:
 *   Done <id> [<err>]
 *     Command with the given id has completed.  If <err> is present, there
 *     was an error.
 *   Command <dev> <addr> <netfn> <cmd> <data>
 *   Event <dev> <data>
 *   Response <dev> <id> <addr> <netfn> <cmd> <data>
 *   ResponseResponse <dev> <id> <addr> <netfn> <cmd> <data>
 *   Shutdown <id>
 *     The program was shut down
 *
 * Note that if the <id> is "-", it means the id couldn't be obtained from
 * the command.
 *
 * Note that <id> and <dev> are decimal numbers.  All other values are
 * hexadecimal.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/ipmi.h>
#include <gensio/gensio.h>
#include <gensio/gensio_list.h>
#include <gensio/argvutils.h>

static void
do_vlog(struct gensio_os_funcs *f, enum gensio_log_levels level,
	const char *log, va_list args)
{
    fprintf(stderr, "gensio %s log: ", gensio_log_level_to_str(level));
    vfprintf(stderr, log, args);
    fprintf(stderr, "\n");
}

struct sendbuf {
    struct gensio_link link;
    gensiods len;
    gensiods pos;
    unsigned char *data;
};

struct ipmiinfo {
    int fd;
    struct gensio_iod *iod;
    bool closing;
    struct accinfo *ai;
    struct gensio_waiter *close_waiter;
};
#define NUM_IPMI_INFO 5

struct ioinfo {
    struct accinfo *ai;
    struct gensio *io;
    struct gensio_link link;

    char inbuf[1024]; /* Holds read data */
    unsigned int inbuf_len;

    /* List of struct sendbuf to write. */
    struct gensio_list writelist;

    bool closing; /* We have started a close */
    bool close_on_write;
};

struct accinfo {
    struct gensio_os_funcs *o;
    struct gensio_waiter *waiter;
    struct gensio_accepter *acc;
    struct gensio_list ios; /* List of ioinfo */
    struct ipmiinfo *ipi; /* Array of IPMI devices. */
    bool shutting_down;
};

static void
shutdown_done(struct gensio_accepter *acc, void *shutdown_data)
{
    struct accinfo *ai = shutdown_data;

    gensio_os_funcs_wake(ai->o, ai->waiter);
}

static void
check_shutdown(struct accinfo *ai)
{
    int rv;

    if (!ai->shutting_down || !gensio_list_empty(&ai->ios))
	return;

    rv = gensio_acc_shutdown(ai->acc, shutdown_done, ai);
    if (rv) {
	fprintf(stderr, "Error shutting down accepter: %s\n",
		gensio_err_to_str(rv));
	shutdown_done(NULL, ai);
    }
}

static void
close_done(struct gensio *io, void *close_data)
{
    struct ioinfo *ii = close_data;
    struct accinfo *ai = ii->ai;

    gensio_free(io);
    gensio_list_rm(&ai->ios, &ii->link);
    free(ii);
    check_shutdown(ai);
}

__attribute__ ((__format__ (__printf__, 2, 3)))
static void
add_output_buf(struct ioinfo *ii, char *str, ...)
{
    va_list ap;
    ssize_t len;
    char dummy;
    struct sendbuf *s;

    va_start(ap, str);
    len = vsnprintf(&dummy, 1, str, ap);
    s = gensio_os_funcs_zalloc(ii->ai->o, sizeof(struct sendbuf) + len + 2);
    va_end(ap);
    if (!s)
	return;
    s->len = len + 1;
    s->pos = 0;
    s->data = ((unsigned char *) s) + sizeof(*s);
    va_start(ap, str);
    vsnprintf((char *) s->data, len + 1, str, ap);
    va_end(ap);
    s->data[len] = '\n';
    s->data[len + 1] = '\0';
    gensio_list_add_tail(&ii->writelist, &s->link);
    gensio_set_write_callback_enable(ii->io, true);
}

static void
start_close(struct ioinfo *ii)
{
    int rv;

    ii->closing = true;
    rv = gensio_close(ii->io, close_done, ii);
    if (rv) {
	/* Should be impossible, but just in case... */
	fprintf(stderr, "Error closing io: %s\n", gensio_err_to_str(rv));
	gensio_list_rm(&ii->ai->ios, &ii->link);
	gensio_free(ii->io);
	free(ii);
    }
}

static bool
get_num(const char *v, unsigned int *onum)
{
    unsigned int num;
    char *end;

    num = strtoul(v, &end, 0);
    if (v[0] == '\0' || *end != '\0')
	return false;
    *onum = num;
    return true;
}

static void
run_cmd(struct ioinfo *ii, unsigned long long id, const char *loadcmdstr)
{
    struct gensio_os_funcs *o = ii->ai->o;
    struct gensio *io;
    int rv, rc;
    gensiods count, pos;
    char buf[1024], ibuf[8], dummy[128];

    rv = str_to_gensio(loadcmdstr, o, NULL, NULL, &io);
    if (rv) {
	add_output_buf(ii, "Done %llu Unable to create gensio %s: %s", id,
		       loadcmdstr, gensio_err_to_str(rv));
	return;
    }

    rv = gensio_open_s(io);
    if (rv) {
	add_output_buf(ii, "Done %llu Unable to open gensio %s: %s", id,
		       loadcmdstr, gensio_err_to_str(rv));
	goto out;
    }

    rv = gensio_set_sync(io);
    if (rv) {
	add_output_buf(ii, "Done %llu Unable to set sync for gensio %s: %s", id,
		       loadcmdstr, gensio_err_to_str(rv));
	goto out;
    }

    rv = 0;
    pos = 0;
    while (rv == 0) {
	if (pos < sizeof(buf) - 1)
	    rv = gensio_read_s(io, &count, buf + pos, sizeof(buf) - pos, NULL);
	else
	    /* Throw away data after the buf size. */
	    rv = gensio_read_s(io, NULL, dummy, sizeof(dummy), NULL);
    }

    rv = GE_NOTREADY;
    while (rv == GE_NOTREADY) {
	count = sizeof(ibuf);
	rv = gensio_control(io, 0, GENSIO_CONTROL_GET, GENSIO_CONTROL_WAIT_TASK,
			    ibuf, &count);
    }
    if (rv) {
	add_output_buf(ii, "Done %llu Unable to wait on gensio %s: %s", id,
		       loadcmdstr, gensio_err_to_str(rv));
	goto out;
    }
    rc = atoi(ibuf);

    rv = gensio_close_s(io);
    if (rv) {
	add_output_buf(ii, "Done %llu Unable to close gensio %s: %s", id,
		       loadcmdstr, gensio_err_to_str(rv));
	goto out;
   }

    if (rc) {
	buf[pos - 1] = '\0';
	add_output_buf(ii, "Done %llu Error executing command %s: %s %s", id,
		       loadcmdstr, ibuf, buf);
    } else {
	add_output_buf(ii, "Done %llu", id);
    }

 out:
    gensio_free(io);
}

static void
ipmi_dev_read_ready(struct gensio_iod *iod, void *cb_data)
{
}

static void
ipmi_dev_cleared(struct gensio_iod *iod, void *cb_data)
{
    struct ipmiinfo *ipi = cb_data;

    gensio_os_funcs_wake(ipi->ai->o, ipi->close_waiter);
}

static void
handle_open(struct ioinfo *ii, unsigned long long id, const char **tokens)
{
    struct ipmiinfo *ipi = ii->ai->ipi;
    struct gensio_os_funcs *o = ii->ai->o;
    unsigned int dev;
    char devstr[128];
    int rv;

    if (!get_num(tokens[0], &dev) || dev >= NUM_IPMI_INFO) {
	add_output_buf(ii, "Done %llu invalid id: %s", id, tokens[0]);
	return;
    }

    if (ipi[dev].fd != -1) {
	add_output_buf(ii, "Done %llu id %s already in use", id, tokens[0]);
	return;
    }

    snprintf(devstr, sizeof(devstr), "/dev/ipmi%u", dev);

    ipi[dev].fd = open(devstr, O_RDWR);
    if (ipi[dev].fd == -1) {
	add_output_buf(ii, "Done %llu Unable to open dev %s: %s", id,
		       devstr, strerror(errno));
	return;
    }

    rv = o->add_iod(o, GENSIO_IOD_DEV, ipi[dev].fd, &ipi[dev].iod);
    if (rv) {
	add_output_buf(ii, "Done %llu Unable to set iod %s: %s", id,
		       devstr, gensio_err_to_str(rv));
	close(ipi[dev].fd);
	ipi[dev].fd = -1;
	return;
    }

    rv = o->set_fd_handlers(ipi[dev].iod, &ipi[dev], ipmi_dev_read_ready,
			    NULL, NULL, ipmi_dev_cleared);
    if (rv) {
	add_output_buf(ii, "Done %llu Unable to setup fd %s: %s", id,
		       devstr, gensio_err_to_str(rv));
	o->close(&ipi[dev].iod);
	ipi[dev].fd = -1;
	return;
    }

    add_output_buf(ii, "Done %llu", id);
}

static void
handle_close(struct ioinfo *ii, unsigned long long id, const char **tokens)
{
    struct ipmiinfo *ipi = ii->ai->ipi;
    struct gensio_os_funcs *o = ii->ai->o;
    unsigned int dev;

    if (!get_num(tokens[0], &dev) || dev >= NUM_IPMI_INFO) {
	add_output_buf(ii, "Done %llu invalid id: %s", id, tokens[0]);
	return;
    }

    if (ipi[dev].fd == -1 || ipi[dev].closing) {
	add_output_buf(ii, "Done %llu id %s not open", id, tokens[0]);
	return;
    }

    ipi[dev].closing = true;
    o->clear_fd_handlers(ipi[dev].iod);
    
    gensio_os_funcs_wait(o, ipi[dev].close_waiter, 1, NULL);
    o->close(&ipi[dev].iod);
    ipi[dev].fd = -1;
    ipi[dev].closing = false;
    add_output_buf(ii, "Done %llu", id);
}

static void
handle_load(struct ioinfo *ii, unsigned long long id, const char **tokens)
{
    char loadcmdstr[128];

    if (!tokens[0]) {
	add_output_buf(ii, "Done %llu No module given", id);
	return;
    }

    snprintf(loadcmdstr, sizeof(loadcmdstr),
	     "stdio(stderr-to-stdout),insmod ipmi_%s.ko", tokens[0]);
    run_cmd(ii, id, loadcmdstr);
}

static void
handle_unload(struct ioinfo *ii, unsigned long long id, const char **tokens)
{
    char loadcmdstr[128];

    if (!tokens[0]) {
	add_output_buf(ii, "Done %llu No module given", id);
	return;
    }

    snprintf(loadcmdstr, sizeof(loadcmdstr),
	     "stdio(stderr-to-stdout),rmmod ipmi_%s", tokens[0]);
    run_cmd(ii, id, loadcmdstr);
}

static void
handle_quit(struct ioinfo *ii, unsigned long long id, const char **tokens)
{
    struct accinfo *ai = ii->ai;
    struct gensio_link *l, *l2;

    ai->shutting_down = true;
    gensio_list_for_each_safe(&ai->ios, l, l2) {
	struct ioinfo *wii = gensio_container_of(l, struct ioinfo, link);

	add_output_buf(wii, "Shutdown %llu", id);

	if (wii == ii) /* Close on the final write. */
	    ii->close_on_write = true;
	else
	    start_close(wii);
    }
    check_shutdown(ai);
}

static struct {
    char *name;
    void (*handler)(struct ioinfo *ii, unsigned long long id,
		    const char **tokens);
} cmds[] = {
    { "Quit", handle_quit },
    { "Open", handle_open },
    { "Close", handle_close },
    { "Load", handle_load },
    { "Unload", handle_unload },
    {}
};

static void
handle_buf(struct ioinfo *ii)
{
    int rv;
    int argc;
    const char **argv;
    unsigned long long id;
    char *end;
    unsigned int i;

    if (ii->closing)
	return;

    rv = gensio_str_to_argv(ii->ai->o, ii->inbuf, &argc, &argv, NULL);
    if (rv)
	return;

    if (argc < 2) {
	add_output_buf(ii, "Done - No id");
	goto out;
    }

    /* id is always second, it will be an unsigned long long. */
    id = strtoull(argv[1], &end, 0);
    if (argv[1][0] == '\0' || *end != '\0') {
	/* Not a valid number. */
	add_output_buf(ii, "Done - Invalid id");
	goto out;
    }

    for (i = 0; cmds[i].name; i++) {
	if (strcmp(cmds[i].name, argv[0]) == 0) {
	    cmds[i].handler(ii, id, argv + 2);
	    goto out;
	}
    }
    add_output_buf(ii, "Done %lld Unknown command: %s", id, argv[0]);
 out:
    gensio_argv_free(ii->ai->o, argv);
}

static int
io_event(struct gensio *io, void *user_data, int event, int err,
	 unsigned char *buf, gensiods *buflen,
	 const char *const *auxdata)
{
    struct ioinfo *ii = user_data;
    gensiods len, i;
    int rv;
    bool handle_it = false;

    switch (event) {
    case GENSIO_EVENT_READ:
	if (ii->closing)
	    return 0;

	if (err) {
	    if (err != GE_REMCLOSE)
		fprintf(stderr, "Error from io: %s\n", gensio_err_to_str(err));
	    start_close(ii);
	    return 0;
	}

	len = *buflen;
	for (i = 0; i < len; i++) {
	    if (buf[i] == '\n' || buf[i] == '\r') {
		ii->inbuf[ii->inbuf_len] = '\0';
		ii->inbuf_len = 0;
		/*
		 * Note that you could continue to process characters
		 * but this demonstrates that you can process partial
		 * buffers, which can sometimes simplify code.
		 */
		handle_it = true;
		i++;
		break;
	    }
	    if (ii->inbuf_len >= sizeof(ii->inbuf) - 1)
		continue;
	    ii->inbuf[ii->inbuf_len++] = buf[i];
	}
	*buflen = i; /* We processed the characters up to the new line. */

	/* Do the response after the echo, if it's ready. */
	if (handle_it)
	    handle_buf(ii);
	return 0;

    case GENSIO_EVENT_WRITE_READY:
	if (ii->closing) {
	    gensio_set_write_callback_enable(ii->io, false);
	    return 0;
	}

	while (!gensio_list_empty(&ii->writelist)) {
	    struct gensio_link *l = gensio_list_first(&ii->writelist);
	    struct sendbuf *sb = gensio_container_of(l, struct sendbuf, link);

	    rv = gensio_write(ii->io, &i, sb->data + sb->pos, sb->len - sb->pos,
			      NULL);
	    if (rv) {
		if (rv != GE_REMCLOSE)
		    fprintf(stderr, "Error writing to io: %s\n",
			    gensio_err_to_str(rv));
		gensio_set_write_callback_enable(ii->io, false);
		start_close(ii);
		return 0;
	    }
	    sb->pos += i;
	    if (sb->pos >= sb->len) {
		gensio_list_rm(&ii->writelist, &sb->link);
		gensio_os_funcs_zfree(ii->ai->o, sb);
	    } else {
		break;
	    }
	}
	if (gensio_list_empty(&ii->writelist)) {
	    gensio_set_write_callback_enable(ii->io, false);
	    if (ii->close_on_write && !ii->closing)
		start_close(ii);
	}
	return 0;

    default:
	return GE_NOTSUP;
    }
}

/*
 * Handle a new connection.
 */
static int
io_acc_event(struct gensio_accepter *accepter, void *user_data,
	     int event, void *data)
{
    struct accinfo *ai = user_data;
    struct ioinfo *ii;

    if (event == GENSIO_ACC_EVENT_LOG) {
	struct gensio_loginfo *li = data;

	vfprintf(stderr, li->str, li->args);
	fprintf(stderr, "\n");
	return 0;
    }

    if (event != GENSIO_ACC_EVENT_NEW_CONNECTION)
	return GE_NOTSUP;

    if (ai->shutting_down) {
	gensio_free(data);
	return 0;
    }

    ii = calloc(1, sizeof(*ii));
    if (!ii) {
	fprintf(stderr, "Could not allocate info for new io\n");
	gensio_free(data);
	return 0;
    }
    ii->io = data;
    ii->ai = ai;
    gensio_list_init(&ii->writelist);
    gensio_list_add_tail(&ai->ios, &ii->link);
    gensio_set_callback(ii->io, io_event, ii);
    gensio_set_read_callback_enable(ii->io, true);
    add_output_buf(ii, "Ready");

    return 0;
}

int
main(int argc, char *argv[])
{
    struct ipmiinfo ipi[NUM_IPMI_INFO];
    struct accinfo ai;
    int rv;
    struct gensio_os_proc_data *proc_data = NULL;
    unsigned int i;

    if (argc < 2) {
	fprintf(stderr, "No gensio accepter given\n");
	return 1;
    }

    memset(&ai, 0, sizeof(ai));
    gensio_list_init(&ai.ios);
    memset(ipi, 0, sizeof(ipi));
    ai.ipi = ipi;

    rv = gensio_alloc_os_funcs(GENSIO_DEF_WAKE_SIG, &ai.o, 0);
    if (rv) {
	fprintf(stderr, "Could not allocate OS handler: %s\n",
		gensio_err_to_str(rv));
	return 1;
    }
    gensio_os_funcs_set_vlog(ai.o, do_vlog);

    rv = gensio_os_proc_setup(ai.o, &proc_data);
    if (rv) {
	fprintf(stderr, "Could not setup process data: %s\n",
		gensio_err_to_str(rv));
	return 1;
    }

    for (i = 0; i < NUM_IPMI_INFO; i++) {
	ipi[i].fd = -1;
	ipi[i].ai = &ai;
	ipi[i].close_waiter = gensio_os_funcs_alloc_waiter(ai.o);
	if (!ipi[i].close_waiter) {
	    fprintf(stderr, "Could not allocate close waiter, out of memory\n");
	    goto out_err;
	}
    }

    ai.waiter = gensio_os_funcs_alloc_waiter(ai.o);
    if (!ai.waiter) {
	rv = GE_NOMEM;
	fprintf(stderr, "Could not allocate waiter, out of memory\n");
	goto out_err;
    }

    rv = str_to_gensio_accepter(argv[1], ai.o, io_acc_event, &ai, &ai.acc);
    if (rv) {
	fprintf(stderr, "Could not allocate %s: %s\n", argv[1],
		gensio_err_to_str(rv));
	goto out_err;
    }

    rv = gensio_acc_startup(ai.acc);
    if (rv) {
	fprintf(stderr, "Could not start %s: %s\n", argv[1],
		gensio_err_to_str(rv));
	goto out_err;
    }

    rv = gensio_os_funcs_wait(ai.o, ai.waiter, 1, NULL);

 out_err:
    if (ai.acc)
	gensio_acc_free(ai.acc);
    if (ai.waiter)
	gensio_os_funcs_free_waiter(ai.o, ai.waiter);
    gensio_os_proc_cleanup(proc_data);
    gensio_os_funcs_free(ai.o);

    return !!rv;
}
