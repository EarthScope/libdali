/***************************************************************************
 * libdali.h:
 * 
 * Interface declarations for the DataLink library (libdali).
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License (GNU-LGPL) for more details.  The
 * GNU-LGPL and further information can be found here:
 * http://www.gnu.org/
 *
 * Written by Chad Trabant
 *   IRIS Data Management Center
 *
 * modified: 2008.017
 ***************************************************************************/


#ifndef LIBDALI_H
#define LIBDALI_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "portable.h"

#define LIBDALI_VERSION "1.0"
#define LIBDALI_RELEASE "2008.013"

#define RECVBUFSIZE         8192     /* Size of receiving buffer */
#define MAXREGEX            8192     /* Maximum regular expression size */
#define MAX_LOG_MSG_LENGTH  200      /* Maximum length of log messages */

/* Define a maximium stream ID string length */
#define MAXSTREAMID 60

/* Return values for dl_collect() and dl_collect_nb() */
#define DLPACKET    1
#define DLTERMINATE 0
#define DLNOPACKET -1

/* Define the high precision time tick interval as 1/modulus seconds *
 * Default modulus of 1000000 defines tick interval as a microsecond */
#define DLTMODULUS 1000000

/* Error code for routines that normally return a high precision time.
 * The time value corresponds to '1902/1/1 00:00:00.000000' with the
 * default DLTMODULUS */
#define DLTERROR -2145916800000000LL

/* Macros to scale between Unix/POSIX epoch time & high precision time */
#define DL_EPOCH2DLTIME(X) X * (dltime_t) DLTMODULUS
#define DL_DLTIME2EPOCH(X) X / DLTMODULUS

/* Require a large (>= 64-bit) integer type for dltime_t */
typedef int64_t dltime_t;

/* Persistent connection state information */
typedef struct DLStat_s
{
  char    databuf[RECVBUFSIZE]; /* Data buffer for received packets */
  int     recptr;               /* Receive pointer for databuf */
  int     sendptr;              /* Send pointer for databuf */
  int8_t  expect_info;          /* Do we expect an INFO response? */

  int8_t  netto_trig;           /* Network timeout trigger */
  int8_t  netdly_trig;          /* Network re-connect delay trigger */
  int8_t  keepalive_trig;       /* Send keepalive trigger */

  double  netto_time;           /* Network timeout time stamp */
  double  netdly_time;          /* Network re-connect delay time stamp */
  double  keepalive_time;       /* Keepalive time stamp */

  enum                          /* Connection state */
    {
      DL_DOWN, DL_UP, DL_DATA
    }
  dl_state;

  enum                          /* INFO query state */
    {
      NoQuery, InfoQuery, KeepAliveQuery
    }
  query_mode;
    
} DLStat;

/* Logging parameters */
typedef struct DLLog_s
{
  void (*log_print)();
  const char * logprefix;
  void (*diag_print)();
  const char * errprefix;
  int  verbosity;
} DLLog;

/* DataLink connection parameters */
typedef struct DLCP_s
{
  char       *addr;             /* The host:port of DataLink server */
  char       *begin_time;	/* Beginning of requested time window */
  char       *end_time;		/* End of requested time window */
  
  int8_t      resume;           /* Boolean flag to control resuming with seq. numbers */
  int8_t      multistation;     /* Boolean flag to indicate multistation mode */
  int8_t      dialup;           /* Boolean flag to indicate dial-up mode */
  int8_t      lastpkttime;      /* Boolean flag to control last packet time usage */
  int8_t      terminate;        /* Boolean flag to control connection termination */

  int         keepalive;        /* Interval to send keepalive/heartbeat (secs) */
  int         netto;            /* Network timeout (secs) */
  int         netdly;           /* Network reconnect delay (secs) */

  float       protocol_ver;     /* Version of the DataLink protocol in use */
  const char *info;             /* INFO level to request */
  int         link;		/* The network socket descriptor */
  DLStat     *stat;             /* Persistent state information */
  DLLog      *log;              /* Logging parameters */
} DLCP;

typedef struct DLPacket_s
{
  char        streamid[MAXSTREAMID]; /* Stream ID */
  dltime_t    pkttime;          /* Packet time */
  dltime_t    datatime;         /* Data time */
  int32_t     datasize;         /* Data size in bytes */
} DLPacket;


/* connection.c */
extern int    dl_collect (DLCP * dlconn, DLPacket ** dlpack);
extern int    dl_collect_nb (DLCP * dlconn, DLPacket ** dlpack);
extern DLCP * dl_newdlcp (void);
extern void   dl_freedlcp (DLCP * dlconn);
extern int    dl_addstream (DLCP * dlconn, const char *net, const char *sta,
			    const char *selectors, int seqnum,
			    const char *timestamp);
extern int    dl_setuniparams (DLCP * dlconn, const char *selectors,
			       int seqnum, const char *timestamp);
extern int    dl_request_info (DLCP * dlconn, const char * infostr);
extern int    dl_sequence (const DLPacket *);
extern int    dl_packettype (const DLPacket *);
extern void   dl_terminate (DLCP * dlconn);

/* config.c */
extern int    dl_read_streamlist (DLCP *dlconn, const char *streamfile,
				  const char *defselect);
extern int    dl_parse_streamlist (DLCP *dlconn, const char *streamlist,
				   const char *defselect);

/* network.c */
extern int    dl_configlink (DLCP * dlconn);
extern int    dl_send_info (DLCP * dlconn, const char * info_level,
			    int verbose);
extern int    dl_connect (DLCP * dlconn, int sayhello);
extern void   dl_disconnect (DLCP * dlconn);
extern int    dl_ping (DLCP * dlconn, char *serverid, char *site);
extern int    dl_senddata (DLCP * dlconn, void *buffer, size_t buflen,
			   const char *ident, void *resp, int resplen);
extern int    dl_recvdata (DLCP * dlconn, void *buffer, size_t maxbytes,
			   const char *ident);
extern int    dl_recvresp (DLCP * dlconn, void *buffer, size_t maxbytes,
                           const char *command, const char *ident);
/* timeutils.c */
extern int    dl_doy2md (int year, int jday, int *month, int *mday);
extern int    dl_md2doy (int year, int month, int mday, int *jday);
extern char  *dl_dltime2isotimestr (dltime_t dltime, char *isotimestr, int8_t subseconds);
extern char  *dl_dltime2mdtimestr (dltime_t dltime, char *mdtimestr, int8_t subseconds);
extern char  *dl_dltime2seedtimestr (dltime_t dltime, char *seedtimestr, int8_t subseconds);
extern dltime_t dl_time2dltime (int year, int day, int hour, int min, int sec, int usec);
extern dltime_t dl_seedtimestr2dltime (char *seedtimestr);
extern dltime_t dl_timestr2dltime (char *timestr);

/* genutils.c */
extern int    dl_bigendianhost (void);
extern double dl_dabs (double value);

/* logging.c */
extern int    dl_log (int level, int verb, ...);
extern int    dl_log_r (const DLCP * dlconn, int level, int verb, ...);
extern int    dl_log_rl (DLLog * log, int level, int verb, ...);
extern void   dl_loginit (int verbosity,
			  void (*log_print)(const char*), const char * logprefix,
			  void (*diag_print)(const char*), const char * errprefix);
extern void   dl_loginit_r (DLCP * dlconn, int verbosity,
			    void (*log_print)(const char*), const char * logprefix,
			    void (*diag_print)(const char*), const char * errprefix);
extern DLLog *dl_loginit_rl (DLLog * log, int verbosity,
			     void (*log_print)(const char*), const char * logprefix,
			     void (*diag_print)(const char*), const char * errprefix);

/* statefile.c */
extern int   dl_recoverstate (DLCP *dlconn, const char *statefile);
extern int   dl_savestate (DLCP *dlconn, const char *statefile);

/* strutils.c */

/* For a linked list of strings, as filled by strparse() */
typedef struct DLstrlist_s {
  char               *element;
  struct DLstrlist_s *next;
} DLstrlist;

extern int  dl_strparse(const char *string, const char *delim, DLstrlist **list);
extern int  dl_strncpclean(char *dest, const char *source, int length);



#ifdef __cplusplus
}
#endif
 
#endif /* LIBDALI_H */
