#ifndef OPENIPMI_WINSOCK_COMPAT_H
#define OPENIPMI_WINSOCK_COMPAT_H

#ifdef _WIN32
#include <winsock2.h>

#define close_socket(f) closesocket(f)

static int socket_set_nonblock(int sock)
{
    unsigned long flags = 1;

    return ioctlsocket(sock, FIONBIO, &flags);
}

static int gen_random(void *data, int len)
{
}

#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define close_socket(f) close(f)

static int socket_set_nonblock(int sock)
{
    return fcntl(sock, F_SETFL, O_NONBLOCK);
}

static int gen_random(void *data, int len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    int rv;

    if (fd == -1)
	return errno;

    while (len > 0) {
	rv = read(fd, data, len);
	if (rv < 0) {
	    rv = errno;
	    goto out;
	}
	len -= rv;
    }

    rv = 0;

 out:
    close(fd);
    return rv;
}

#endif

#endif /* OPENIPMI_WINSOCK_COMPAT_H */
