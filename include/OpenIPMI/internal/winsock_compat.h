#ifndef OPENIPMI_WINSOCK_COMPAT_H
#define OPENIPMI_WINSOCK_COMPAT_H

#ifdef _WIN32
#define close_socket(f) closesocket(f)
#else
#define close_socket(f) close(f)
#endif

#endif /* OPENIPMI_WINSOCK_COMPAT_H */
