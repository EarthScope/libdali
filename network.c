
/***************************************************************************
 * network.c
 *
 * Network communication routines for DataLink
 *
 * Written by Chad Trabant, 
 *   IRIS Data Management Center
 *
 * Version: 2008.030
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libdali.h"


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
dl_connect (DLCP *dlconn)
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
  
  /* socket connected */
  dl_log_r (dlconn, 1, 1, "[%s] network socket opened\n", dlconn->addr);
  
  dlconn->link = sock;
  
  /* Everything should be connected, exchange IDs */
  if ( dl_exchangeID (dlconn) == -1 )
    {
      slp_sockclose (sock);
      return -1;
    }
  
  return sock;
}  /* End of dl_connect() */


/***************************************************************************
 * dl_disconnect:
 *
 * Close the network socket associated with connection
 *
 ***************************************************************************/
void
dl_disconnect (DLCP *dlconn)
{
  if ( dlconn->link > 0 )
    {
      slp_sockclose (dlconn->link);
      dlconn->link = -1;
      
      dl_log_r (dlconn, 1, 1, "[%s] network socket closed\n", dlconn->addr);
    }
}  /* End of dl_disconnect() */


/***************************************************************************
 * dl_exchangeID:
 *
 * Send the ID command including the client ID and parse the
 * capability flags from the returned string.
 *
 * Returns -1 on errors, 0 on success.
 ***************************************************************************/
int
dl_exchangeID (DLCP *dlconn)
{
  int ret = 0;
  int servcnt = 0;
  int sitecnt = 0;
  char sendstr[255];		/* Buffer for command strings */
  char recvstr[255];		/* Buffer for server response */
  char *capptr;                 /* Pointer to capabilities flags */  
  char capflag = 0;             /* Capabilities are supported by server */
  
  /* Send ID command including client ID */
  sprintf (sendstr, "ID %s", dlconn->clientid);
  dl_log_r (dlconn, 1, 2, "[%s] sending: %s\n", dlconn->addr, sendstr);
  
  if ( dl_sendpacket (dlconn, sendstr, strlen (sendstr), NULL, 0,
		      recvstr, sizeof(recvstr)) < 0 )
    {
      return -1;
    }
  
  /* Verify DataLink signature in server response */
  if ( strncasecmp (recvstr, "DATALINK", 8) )
    {
      dl_log_r (dlconn, 1, 1,
                "[%s] unrecognized server ID: %8.8s\n",
                dlconn->addr, recvstr);
      return -1;
    }
  
  /* Search for capabilities flags in server ID by looking for "::"
   * The expected format of the complete server ID is:
   * "DataLink <optional text> <:: optional capability flags>"
   */
  capptr = strstr (recvstr, "::");
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
  
  /* Report received server ID */
  dl_log_r (dlconn, 1, 1, "[%s] connected to: %s\n", dlconn->addr, recvstr);
  if ( capptr )
    dl_log_r (dlconn, 1, 1, "[%s] capabilities: %s\n", dlconn->addr, capptr);
  
  /* Check capabilities flags */
  if ( capptr )
    {
      char *tptr;
      
      /* Parse protocol version flag: "DLPROTO:<#.#>" if present */
      if ( (tptr = strstr(capptr, "DLPROTO")) )
	{
	  /* This protocol specification overrides that from earlier in the server ID */
	  ret = sscanf (tptr, "DLPROTO:%f", &dlconn->serverproto);
	  
	  if ( ret != 1 )
	    dl_log_r (dlconn, 1, 1,
		      "[%s] could not parse protocol version from DLPROTO flag: %s\n",
		      dlconn->addr, tptr);
	}
    }
  
  return 0;
}  /* End of dl_exchangeID() */


/***************************************************************************
 * dl_senddata:
 *
 * send() 'sendlen' bytes from 'buffer' to 'dlconn->link'.
 *
 * Returns 0 on success and -1 on error.
 ***************************************************************************/
int
dl_senddata (DLCP *dlconn, void *buffer, size_t sendlen)
{
  /* Set socket to blocking */
  if ( dlp_sockblock (dlconn->link) )
    {
      dl_log_r (dlconn, 2, 0, "[%s] error setting socket to blocking\n",
		dlconn->addr);
      return -1;
    }
  
  /* Send data */
  if ( send (dlconn->link, buffer, sendlen, 0) < 0 )
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
  
  return 0;
}  /* End of dl_senddata() */


/***************************************************************************
 * dl_sendpacket:
 *
 * Send a DataLink packet created by combining 'headerbuf' with
 * 'packetbuf' to 'dlconn->link'.  A 3-byte pre-header composed of 2
 * synchronization bytes of 'DL' followed by an 8-byte unsigned
 * integer that is the length of the payload will be created and
 * prepended to the packet.
 *
 * The header length must be larger than 0 but the packet length can
 * be 0 resulting in a header-only packet commonly used for sendind
 * commands.
 *
 * If 'resp' is not NULL then read up to 'resplen' bytes into 'resp'
 * after sending 'buffer' using dl_recvheader().  This is only
 * designed for small pieces of data, specifically the server
 * acknowledgement to a command which are a packet header only.
 *
 * Returns -1 on error, and size (in bytes) of the response
 * received (0 if 'resp' == NULL).
 ***************************************************************************/
int
dl_sendpacket (DLCP *dlconn, void *headerbuf, size_t headerlen,
	       void *packetbuf, size_t packetlen,
	       void *resp, int resplen)
{
  int bytesread = 0;		/* bytes read into resp buffer */
  char wirepacket[MAXPACKETSIZE];
  
  if ( ! dlconn || ! headerbuf )
    return -1;
  
  /* Sanity check that the header is not too large or zero */
  if ( headerlen > 255 || headerlen == 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] packet header size is invalid: %d\n",
		dlconn->addr, headerlen);
      return -1;
    }
  
  /* Sanity check that the header + packet data is not too large */
  if ( (3 + headerlen + packetlen) > MAXPACKETSIZE )
    {
      dl_log_r (dlconn, 2, 0, "[%s] packet is too large (%d), max is %d\n",
		dlconn->addr, (headerlen + packetlen), MAXPACKETSIZE);
      return -1;
    }
  
  /* Set the synchronization and header size bytes */
  wirepacket[0] = 'D';
  wirepacket[1] = 'L';
  wirepacket[2] = (uint8_t) headerlen;
  
  /* Copy header into the wire packet */
  memcpy (wirepacket+3, headerbuf, headerlen);
  
  /* Copy packet data into the wire packet if supplied */
  if ( packetbuf && packetlen > 0 )
    memcpy (wirepacket+3+headerlen, packetbuf, packetlen);
  
  /* Send data */
  if ( dl_senddata (dlconn, wirepacket, (3+headerlen+packetlen)) < 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] error sending data\n", dlconn->addr);
      return -1;
    }
  
  /* If requested collect the response (packet header only) */
  if ( resp != NULL )
    {
      if ( (bytesread = dl_recvheader (dlconn, resp, resplen)) < 0 )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] error receving data\n", dlconn->addr);
	  return -1;
	}
    }
  
  return bytesread;
}  /* End of dl_sendpacket() */


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
			dlconn->addr, nrecv, dlp_strerror ());
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
  char *cbuffer = buffer;
  
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
  if ( cbuffer[0] != 'D' || cbuffer[1] != 'L' )
    {
      dl_log_r (dlconn, 2, 0, "[%s] No DataLink packet detected\n",
		dlconn->addr);
      return -1;
    }
  
  /* 3rd byte is the header length */
  headerlen = (uint8_t) cbuffer[2];
  
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
