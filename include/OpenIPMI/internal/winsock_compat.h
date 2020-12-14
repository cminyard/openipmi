#ifndef OPENIPMI_WINSOCK_COMPAT_H
#define OPENIPMI_WINSOCK_COMPAT_H

#ifdef _WIN32
#include <winsock2.h>

#define close_socket(f) closesocket(f)

#define socket_set_nonblock(sock)	  \
    ({					  \
    unsigned long flags = 1;		  \
    ioctlsocket(sock, FIONBIO, &flags);	  \
    })

#define gen_random(data, len)

#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define close_socket(f) close(f)

#define socket_set_nonblock(sock) fcntl(sock, F_SETFL, O_NONBLOCK);

#define gen_random(data, len)				\
    ({							\
    int fd = open("/dev/urandom", O_RDONLY);		\
    int rv = 0;						\
    if (fd == -1)					\
	return errno;					\
    while (len > 0) {					\
	rv = read(fd, data, len);			\
	if (rv < 0) {					\
	    rv = errno;					\
	    break;					\
	}						\
	len -= rv;					\
	rv = 0;						\
    }							\
    close(fd);						\
    rv;							\
    })

#endif

#endif /* OPENIPMI_WINSOCK_COMPAT_H */
