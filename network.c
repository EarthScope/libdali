
/***************************************************************************
 * network.c
 *
 * Network communication routines for DataLink
 *
 * Written by Chad Trabant, 
 *   IRIS Data Management Center
 *
 * Version: 2008.025
 ***************************************************************************/

CHAD, still need processing:
  dl_sayhello() :: should be dl_getID()?
  dl_ping()

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libdali.h"

/* Functions only used in this source file */
int dl_sayhello (DLCP * dlconn);


/***************************************************************************
 * dl_connect:
 *
 * Open a network socket connection to a Datalink server and set
 * 'dlconn->link' to the new descriptor.  Expects 'dlconn->addr' to
 * be in 'host:port' format.  Either the host, port or both are
 * optional, if the host is not specified 'localhost' is assumed, if
 * the port is not specified '16000' is assumed, if neither is
 * specified (only a colon) then 'localhost' and port '16000' are
 * assumed.
 *
 * If a permanent error is detected (invalid port specified) the
 * dlconn->terminate flag will be set so the dl_collect() family of
 * routines will not continue trying to connect.
 *
 * Returns -1 on errors otherwise the socket descriptor created.
 ***************************************************************************/
int
dl_connect (DLCP * dlconn)
{
  int sock;
  int on = 1;
  int sockstat;
  long int nport;
  char nodename[300];
  char nodeport[100];
  char *ptr, *tail;
  size_t addrlen;
  struct sockaddr addr;
  
  if ( slp_sockstartup() )
    {
      dl_log_r (dlconn, 2, 0, "could not initialize network sockets\n");
      return -1;
    }
  
  /* Check server address string and use defaults if needed:
   * If only ':' is specified neither host nor port specified
   * If no ':' is included no port was specified
   * If ':' is the first character no host was specified
   */
  if ( ! strcmp (dlconn->addr, ":") )
    {
      strcpy (nodename, "localhost");
      strcpy (nodeport, "16000");
    }
  else if ((ptr = strchr (dlconn->addr, ':')) == NULL)
    {
      strncpy (nodename, dlconn->addr, sizeof(nodename));
      strcpy (nodeport, "16000");
    }
  else
    {
      if ( ptr == dlconn->addr )
	{
	  strcpy (nodename, "localhost");
	}
      else
	{
	  strncpy (nodename, dlconn->addr, (ptr - dlconn->addr));
	  nodename[(ptr - dlconn->addr)] = '\0';
	}
      
      strcpy (nodeport, ptr+1);

      /* Sanity test the port number */
      nport = strtoul (nodeport, &tail, 10);
      if ( *tail || (nport <= 0 || nport > 0xffff) )
	{
	  dl_log_r (dlconn, 2, 0, "server port specified incorrectly\n");
	  dlconn->terminate = 1;
	  return -1;
	}
    }
  
  /* Resolve server address */
  if ( slp_getaddrinfo (nodename, nodeport, &addr, &addrlen) )
    {
      dl_log_r (dlconn, 2, 0, "cannot resolve hostname %s\n", nodename );
      return -1;
    }
  
  /* Create socket */
  if ( (sock = socket (PF_INET, SOCK_STREAM, 0)) < 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] socket(): %s\n", dlconn->addr, slp_strerror ());
      slp_sockclose (sock);
      return -1;
    }
  
  /* Connect socket */
  if ( (slp_sockconnect (sock, (struct sockaddr *) &addr, addrlen)) )
    {
      dl_log_r (dlconn, 2, 0, "[%s] connect(): %s\n", dlconn->addr, slp_strerror ());
      slp_sockclose (sock);
      return -1;
    }
  
  /* Set socket to non-blocking */
  if ( slp_socknoblock(sock) )
    {
      dl_log_r (dlconn, 2, 0, "Error setting socket to non-blocking\n");
      return -1;
    }
  
  if ( ! dlconn->terminate )
    { /* socket connected */
      dl_log_r (dlconn, 1, 1, "[%s] network socket opened\n", dlconn->addr);
      
      dlconn->link = sock;
      
      /* Everything should be connected, say hello if requested */
      if ( sayhello )
	{
	  if (dl_sayhello (dlconn) == -1)
	    {
	      slp_sockclose (sock);
	      return -1;
	    }
	}
      
      return sock;
    }
  
  return -1;
}  /* End of dl_connect() */


/***************************************************************************
 * dl_disconnect:
 *
 * Close the network socket associated with connection
 *
 ***************************************************************************/
void
dl_disconnect (DLCP * dlconn)
{
  if ( dlconn->link > 0 )
    {
      slp_sockclose (dlconn->link);
      dlconn->link = -1;
      
      dl_log_r (dlconn, 1, 1, "[%s] network socket closed\n", dlconn->addr);
    }
}  /* End of dl_disconnect() */


/***************************************************************************
 * dl_sayhello:
 *
 * Send the HELLO command and attempt to parse the server version
 * number from the returned string.  The server version is set to 0.0
 * if it can not be parsed from the returned string, which indicates
 * unkown protocol functionality.
 *
 * Returns -1 on errors, 0 on success.
 ***************************************************************************/
int
dl_sayhello (DLCP * dlconn)
{
  int ret = 0;
  int servcnt = 0;
  int sitecnt = 0;
  char sendstr[100];		/* A buffer for command strings */
  char servstr[200];		/* The remote server ident */
  char sitestr[100];		/* The site/data center ident */
  char servid[100];		/* Server ID string, i.e. 'Datalink' */
  char *capptr;                 /* Pointer to capabilities flags */  
  char capflag = 0;             /* Capabilities are supported by server */

  /* Send HELLO */
  sprintf (sendstr, "HELLO\r");
  dl_log_r (dlconn, 1, 2, "[%s] sending: %s\n", dlconn->addr, sendstr);
  dl_senddata (dlconn, (void *) sendstr, strlen (sendstr), dlconn->addr,
	       NULL, 0);
  
  /* Recv the two lines of response: server ID and site installation ID */
  if ( dl_recvresp (dlconn, (void *) servstr, (size_t) sizeof (servstr),
		    sendstr, dlconn->addr) < 0 )
    {
      return -1;
    }
  
  if ( dl_recvresp (dlconn, (void *) sitestr, (size_t) sizeof (sitestr),
		    sendstr, dlconn->addr) < 0 )
    {
      return -1;
    }
  
  /* Terminate on first "\r" character or at one character before end of buffer */
  servcnt = strcspn (servstr, "\r");
  if ( servcnt > (sizeof(servstr)-2) )
    {
      servcnt = (sizeof(servstr)-2);
    }
  servstr[servcnt] = '\0';
  
  sitecnt = strcspn (sitestr, "\r");
  if ( sitecnt > (sizeof(sitestr)-2) )
    {
      sitecnt = (sizeof(sitestr)-2);
    }
  sitestr[sitecnt] = '\0';
  
  /* Search for capabilities flags in server ID by looking for "::"
   * The expected format of the complete server ID is:
   * "datalink v#.# <optional text> <:: optional capability flags>"
   */
  capptr = strstr (servstr, "::");
  if ( capptr )
    {
      /* Truncate server ID portion of string */
      *capptr = '\0';

      /* Move pointer to beginning of flags */
      capptr += 2;
      
      /* Move capptr up to first non-space character */
      while ( *capptr == ' ' )
	capptr++;
    }
  
  /* Report received IDs */
  dl_log_r (dlconn, 1, 1, "[%s] connected to: %s\n", dlconn->addr, servstr);
  if ( capptr )
    dl_log_r (dlconn, 1, 1, "[%s] capabilities: %s\n", dlconn->addr, capptr);
  dl_log_r (dlconn, 1, 1, "[%s] organization: %s\n", dlconn->addr, sitestr);
  
  /* Parse old-school server ID and version from the returned string.
   * The expected format at this point is:
   * "datalink v#.# <optional text>"
   * where 'datalink' is case insensitive and '#.#' is the server/protocol version.
   */
  /* Add a space to the end to allowing parsing when the optionals are not present */
  servstr[servcnt] = ' '; servstr[servcnt+1] = '\0';
  ret = sscanf (servstr, "%s v%f ", &servid[0], &dlconn->protocol_ver);
  
  if ( ret != 2 || strncasecmp (servid, "DATALINK", 8) )
    {
      dl_log_r (dlconn, 1, 1,
                "[%s] unrecognized server version, assuming minimum functionality\n",
                dlconn->addr);
      dlconn->protocol_ver = 0.0;
    }
  
  /* Check capabilities flags */
  if ( capptr )
    {
      char *tptr;
      
      /* Parse protocol version flag: "SLPROTO:<#.#>" if present */
      if ( (tptr = strstr(capptr, "SLPROTO")) )
	{
	  /* This protocol specification overrides that from earlier in the server ID */
	  ret = sscanf (tptr, "SLPROTO:%f", &dlconn->protocol_ver);
	  
	  if ( ret != 1 )
	    dl_log_r (dlconn, 1, 1,
		      "[%s] could not parse protocol version from SLPROTO flag: %s\n",
		      dlconn->addr, tptr);
	}
      
      /* Check for CAPABILITIES command support */
      if ( strstr(capptr, "CAP") )
	capflag = 1;
    }
  
  /* Send CAPABILITIES flags if supported by server */
  if ( capflag )
    {
      int bytesread = 0;
      char readbuf[100];
      
      char *term1, *term2;
      char *extreply = 0;
      
      /* Current capabilities:
       *   SLPROTO:3.0 = Datalink protocol version 3.0
       *   CAP         = CAPABILITIES command support
       *   EXTREPLY    = Extended reply message handling
       *   NSWILDCARD  = Network and station code wildcard support
       */
      sprintf (sendstr, "CAPABILITIES SLPROTO:3.0 CAP EXTREPLY NSWILDCARD\r");
      
      /* Send CAPABILITIES and recv response */
      dl_log_r (dlconn, 1, 2, "[%s] sending: %s\n", dlconn->addr, sendstr);
      bytesread = dl_senddata (dlconn, (void *) sendstr, strlen (sendstr), dlconn->addr,
			       readbuf, sizeof (readbuf));
      
      if ( bytesread < 0 )
	{		/* Error from dl_senddata() */
	  return -1;
	}
      
      /* Search for 2nd "\r" indicating extended reply message present */
      extreply = 0;
      if ( (term1 = memchr (readbuf, '\r', bytesread)) )
	{
	  if ( (term2 = memchr (term1+1, '\r', bytesread-(readbuf-term1)-1)) )
	    {
	      *term2 = '\0';
	      extreply = term1+1;
	    }
	}
      
      /* Check response to CAPABILITIES */
      if (!strncmp (readbuf, "OK\r", 3) && bytesread >= 4)
	{
	  dl_log_r (dlconn, 1, 2, "[%s] capabilities OK %s%s%s\n", dlconn->addr,
		    (extreply)?"{":"", (extreply)?extreply:"", (extreply)?"}":"");
	}
      else if (!strncmp (readbuf, "ERROR\r", 6) && bytesread >= 7)
	{
	  dl_log_r (dlconn, 1, 2, "[%s] CAPABILITIES not accepted %s%s%s\n", dlconn->addr,
		    (extreply)?"{":"", (extreply)?extreply:"", (extreply)?"}":"");	  
	  return -1;
	}
      else
	{
	  dl_log_r (dlconn, 2, 0,
		    "[%s] invalid response to CAPABILITIES command: %.*s\n",
		    dlconn->addr, bytesread, readbuf);
	  return -1;
	}
    }
  
  return 0;
}  /* End of dl_sayhello() */


/***************************************************************************
 * dl_ping:
 *
 * Connect to a server, issue the HELLO command, parse out the server
 * ID and organization resonse and disconnect.  The server ID and
 * site/organization strings are copied into serverid and site strings
 * which should have 100 characters of space each.
 *
 * Returns:
 *   0  Success
 *  -1  Connection opened but invalid response to 'HELLO'.
 *  -2  Could not open network connection
 ***************************************************************************/
int
dl_ping (DLCP * dlconn, char *serverid, char *site)
{
  int servcnt = 0;
  int sitecnt = 0;
  char sendstr[100];		/* A buffer for command strings */
  char servstr[100];		/* The remote server ident */
  char sitestr[100];		/* The site/data center ident */
  
  /* Open network connection to server */
  if ( dl_connect (dlconn, 0) == -1 )
    {
      dl_log_r (dlconn, 2, 1, "Could not connect to server\n");
      return -2;
    }
  
  /* Send HELLO */
  sprintf (sendstr, "HELLO\r");
  dl_log_r (dlconn, 1, 2, "[%s] sending: HELLO\n", dlconn->addr);
  dl_senddata (dlconn, (void *) sendstr, strlen (sendstr), dlconn->addr,
	       NULL, 0);
  
  /* Recv the two lines of response */
  if ( dl_recvresp (dlconn, (void *) servstr, (size_t) sizeof (servstr), 
		    sendstr, dlconn->addr) < 0 )
    {
      return -1;
    }
  
  if ( dl_recvresp (dlconn, (void *) sitestr, (size_t) sizeof (sitestr),
		    sendstr, dlconn->addr) < 0 )
    {
      return -1;
    }
  
  servcnt = strcspn (servstr, "\r");
  if ( servcnt > 90 )
    {
      servcnt = 90;
    }
  servstr[servcnt] = '\0';
  
  sitecnt = strcspn (sitestr, "\r");
  if ( sitecnt > 90 )
    {
      sitecnt = 90;
    }
  sitestr[sitecnt] = '\0';
  
  /* Copy the response strings into the supplied strings */
  strncpy (serverid, servstr, sizeof(servstr));
  strncpy (site, sitestr, sizeof(sitestr));
  
  dl_disconnect (dlconn);
  
  return 0;
}  /* End of dl_ping() */


/***************************************************************************
 * dl_senddata:
 *
 * send() 'buflen' bytes from 'buffer' to 'dlconn->link'.  'ident' is
 * a string to include in error messages for identification, usually
 * the address of the remote server.  If 'resp' is not NULL then read
 * up to 'resplen' bytes into 'resp' after sending 'buffer'.  This is
 * only designed for small pieces of data, specifically the server
 * acknowledgement to a command.
 *
 * Returns -1 on error, and size (in bytes) of the response
 * received (0 if 'resp' == NULL).
 ***************************************************************************/
int
dl_senddata (DLCP *dlconn, void *buffer, size_t buflen,
	     void *resp, int resplen)
{
  int bytesread = 0;		/* bytes read into resp */
  
  /* Set socket to blocking */
  if ( dlp_sockblock (dlconn->link) )
    {
      dl_log_r (dlconn, 2, 0, "[%s] error setting socket to blocking\n",
		dlconn->addr);
      return -1;
    }
  
  /* Send data */
  if ( send (dlconn->link, buffer, buflen, 0) < 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] error sending data\n", dlconn->addr);
      return -1;
    }
  
  /* Set socket to non-blocking */
  if ( dlp_socknoblock (dlconn->link) )
    {
      dl_log_r (dlconn, 2, 0, "[%s] error setting socket to non-blocking\n",
		dlconn->addr);
      return -1;
    }
  
  /* If requested collect the response */
  if ( resp != NULL )
    {
      /* Clear response buffer */
      memset (resp, 0, resplen);
      
      bytesread = dl_recvheader (dlconn, resp, resplen);
    }
  
  return bytesread;
}  /* End of dl_senddata() */


/***************************************************************************
 * dl_recvdata:
 *
 * recv() 'readlen' bytes from 'dlconn->link' into a specified
 * 'buffer'.
 *
 * Return number of characters read on success, -1 on connection
 * shutdown and -2 on error.
 ***************************************************************************/
int
dl_recvdata (DLCP *dlconn, void *buffer, size_t readlen)
{
  int nrecv;
  int nread = 0;
  char *bptr = buffer;
  
  if ( ! buffer )
    {
      return -2;
    }
  
  /* Recv until readlen bytes have been read */
  while ( nread < readlen )
    {
      if ( (nrecv = recv(dlconn->link, bptr, readlen-nread, 0)) < 0 )
        {
          /* The only acceptable error is no data on non-blocking */
	  if ( slp_noblockcheck() )
	    {
	      dl_log_r (dlconn, 2, 0, "[%s] recv():%d %s\n",
			dlconn->addr, bytesread, dlp_strerror ());
	      return -2;
	    }
        }
      
      /* Peer completed an orderly shutdown */
      if ( nrecv == 0 )
        return -1;
      
      /* Update recv pointer and byte count */
      if ( nrecv > 0 )
        {
          bptr += nrecv;
          nread += nrecv;
        }
    }
  
  return nread;
}  /* End of dl_recvdata() */


/***************************************************************************
 * dl_recvheader:
 *
 * Receive a DataLink packet header composed of two sequence bytes
 * ("DL"), followed a packet data length byte, followed by a header
 * payload.
 *
 * It should not be assumed that the populated buffer contains a
 * terminated string.
 *
 * Returns -1 on error or shutdown and the number of bytes in the
 * header payload on success.
 ***************************************************************************/
int
dl_recvheader (DLCP *dlconn, void *buffer, size_t buflen)
{
  int bytesread = 0;
  int headerlen;
  
  if ( ! buffer )
    {
      return -1;
    }
  
  if ( buflen < 255 )
    {
      return -1;
    }
  
  /* Receive synchronization bytes and header length */
  if ( (bytesread = sl_recvdata (buffer, buffer, 3)) != 3 )
    {
      return -1;
    }
  
  /* Test synchronization bytes */
  if ( buffer[0] != 'R' || buffer[1] != 'S' )
    {
      dl_log_r (dlconn, 2, 0, "[%s] No DataLink packet detected\n",
		dlconn->addr);
      return -1;
    }
  
  /* 3rd byte is the header length */
  headerlen = (unt8_t) buffer[2];
  
  /* Receive header payload */
  if ( (bytesread = dl_recvdata (dlconn, buffer, headerlen)) != headerlen )
    {
      return -1;
    }
  
  /* Trap door for termination */
  if ( dlconn->terminate )
    {
      return -1;
    }
  
  return bytesread;
}  /* End of dl_recvheader() */
