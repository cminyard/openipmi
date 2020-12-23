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

#include <bcrypt.h>
#include <ntstatus.h>

#define gen_random(data, len)		\
    ({									\
    NTSTATUS grv;							\
    BCRYPT_ALG_HANDLE alg;						\
    grv = BCryptOpenAlgorithmProvider(&alg, BCRYPT_RSA_ALGORITHM,	\
				      MS_PRIMITIVE_PROVIDER, 0);	\
    if (grv != STATUS_SUCCESS)						\
	return grv;							\
    grv = BCryptGenRandom(alg, data, len, 0);				\
    BCryptCloseAlgorithmProvider(alg, 0);				\
    grv;								\
    })

#define network_init()	\
    ({									\
    WSADATA wsaData;							\
    int err;								\
    err = WSAStartup(MAKEWORD(2, 2), &wsaData);				\
    if (err != 0)							\
	err = ENOTSUP;							\
    err;								\
    })

#define network_shutdown() WSACleanup();

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

#define network_init() 0
#define network_shutdown() do {} while(0)

#endif

#endif /* OPENIPMI_WINSOCK_COMPAT_H */
