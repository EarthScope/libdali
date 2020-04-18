/***********************************************************************//**
 * @file portable.h:
 *
 * Platform specific headers for platorm portable routines.
 *
 * This file is part of the DataLink Library.
 *
 * Copyright (c) 2020 Chad Trabant, IRIS Data Management Center
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
