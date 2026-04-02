/* dos-compat/sys/socket.h
 * Stub for DOS/OpenWatcom builds. WolfSSL includes this via wolfio.h when
 * __LINUX__ is defined (OpenWatcom on Linux host). All actual socket I/O
 * is handled by mTCP callbacks in tls_io.c -- none of these types are used.
 */
#ifndef DOS_COMPAT_SYS_SOCKET_H
#define DOS_COMPAT_SYS_SOCKET_H
#endif
