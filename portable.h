/***********************************************************************//**
 * @file portable.h:
 *
 * Platform specific headers.  This file provides a basic level of platform
 * portability.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License (GNU-LGPL) for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this software.
 * If not, see <https://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2016 Chad Trabant, IRIS Data Management Center
 *
 * modified: 2016.291
 ***************************************************************************/

#ifndef PORTABLE_H
#define PORTABLE_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "libdali.h"

extern int dlp_sockstartup (void);
extern int dlp_sockconnect (SOCKET socket, struct sockaddr * inetaddr, int addrlen);
extern int dlp_sockclose (SOCKET socket);
extern int dlp_sockblock (SOCKET socket);
extern int dlp_socknoblock (SOCKET socket);
extern int dlp_noblockcheck (void);
extern int dlp_setsocktimeo (SOCKET socket, int timeout);
extern int dlp_setioalarm (int timeout);
extern int dlp_openfile (const char *filename, char perm);
extern const char *dlp_strerror (void);
extern int64_t dlp_time (void);
extern void dlp_usleep (unsigned long int useconds);
extern int dlp_genclientid (char *progname, char *clientid, size_t maxsize);

#ifdef __cplusplus
}
#endif

#endif /* PORTABLE_H */
