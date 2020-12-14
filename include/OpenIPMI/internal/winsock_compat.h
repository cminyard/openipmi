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

#endif

#endif /* OPENIPMI_WINSOCK_COMPAT_H */
