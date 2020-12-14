#ifndef OPENIPMI_WINSOCK_COMPAT_H
#define OPENIPMI_WINSOCK_COMPAT_H

#ifdef _WIN32
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600     //fix missing inet_pton
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

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
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/poll.h>

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
