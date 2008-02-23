
/***************************************************************************
 * network.c
 *
 * Network communication routines for DataLink
 *
 * Written by Chad Trabant, 
 *   IRIS Data Management Center
 *
 * Version: 2008.053
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
  long int nport;
  char nodename[300];
  char nodeport[100];
  char *ptr, *tail;
  size_t addrlen;
  struct sockaddr addr;
  
  if ( dlp_sockstartup() )
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
  if ( dlp_getaddrinfo (nodename, nodeport, &addr, &addrlen) )
    {
      dl_log_r (dlconn, 2, 0, "cannot resolve hostname %s\n", nodename );
      return -1;
    }
  
  /* Create socket */
  if ( (sock = socket (PF_INET, SOCK_STREAM, 0)) < 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] socket(): %s\n", dlconn->addr, dlp_strerror ());
      dlp_sockclose (sock);
      return -1;
    }
  
  /* Connect socket */
  if ( (dlp_sockconnect (sock, (struct sockaddr *) &addr, addrlen)) )
    {
      dl_log_r (dlconn, 2, 0, "[%s] connect(): %s\n", dlconn->addr, dlp_strerror ());
      dlp_sockclose (sock);
      return -1;
    }
  
  /* Set socket to non-blocking */
  if ( dlp_socknoblock(sock) )
    {
      dl_log_r (dlconn, 2, 0, "Error setting socket to non-blocking\n");
      dlp_sockclose (sock);
      return -1;
    }
  
  /* socket connected */
  dl_log_r (dlconn, 1, 1, "[%s] network socket opened\n", dlconn->addr);
  
  dlconn->link = sock;
  
  /* Everything should be connected, exchange IDs */
  if ( dl_getid (dlconn, 1) == -1 )
    {
      dlp_sockclose (sock);
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
      dlp_sockclose (dlconn->link);
      dlconn->link = -1;
      
      dl_log_r (dlconn, 1, 1, "[%s] network socket closed\n", dlconn->addr);
    }
}  /* End of dl_disconnect() */


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
 * be 0 resulting in a header-only packet commonly used for sending
 * commands.
 *
 * If 'resp' is not NULL then read up to 'resplen' bytes into 'resp'
 * after sending 'buffer' using dl_recvheader().  This is only
 * designed for small pieces of data, specifically the server
 * acknowledgement to a command, which are a packet header only.
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
      if ( (bytesread = dl_recvheader (dlconn, resp, resplen, 1)) < 0 )
	{
	  if ( bytesread < -1 )
	    dl_log_r (dlconn, 2, 0, "[%s] error receiving data\n", dlconn->addr);
	  
	  return -1;
	}
    }
  
  return bytesread;
}  /* End of dl_sendpacket() */


/***************************************************************************
 * dl_recvdata:
 *
 * recv() 'readlen' bytes from 'dlconn->link' into a specified
 * 'buffer'.  If the 'blockflag' is true the socket is set to blocking
 * while receiving and set back to non-blocking after done receiving.
 *
 * Return number of bytes read on success (0 when no data available on
 * non-blocking socket), -1 on connection shutdown and -2 on error.
 ***************************************************************************/
int
dl_recvdata (DLCP *dlconn, void *buffer, size_t readlen, uint8_t blockflag)
{
  int nrecv;
  int nread = 0;
  char *bptr = buffer;
  
  if ( ! buffer )
    {
      return -2;
    }
  
  /* Set socket to blocking if requested */
  if ( blockflag )
    {
      if ( dlp_sockblock (dlconn->link) )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] Error setting socket to blocking: %s\n",
		    dlconn->addr, dlp_strerror ());
	  return -2;
	}
    }

  /* Recv until readlen bytes have been read */
  while ( nread < readlen )
    {
      if ( (nrecv = recv(dlconn->link, bptr, readlen-nread, 0)) < 0 )
        {
          /* The only acceptable error is no data on non-blocking */
	  if ( ! blockflag && ! dlp_noblockcheck() )
	    {
	      nread = 0;
	    }
	  else
	    {	      
	      dl_log_r (dlconn, 2, 0, "[%s] recv(%d): %d %s\n",
			dlconn->addr, dlconn->link, nrecv, dlp_strerror ());
	      nread = -2;
	    }
	  
	  break;
        }
      
      /* Peer completed an orderly shutdown */
      if ( nrecv == 0 )
	{
	  nread = -1;
	  break;
	}
      
      /* Update recv pointer and byte count */
      if ( nrecv > 0 )
        {
          bptr += nrecv;
          nread += nrecv;
        }
    }
  
  /* Set socket to non-blocking if set to blocking */
  if ( blockflag )
    {
      if ( dlp_socknoblock (dlconn->link) )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] Error setting socket to non-blocking: %s\n",
		    dlconn->addr, dlp_strerror ());
	  return -2;
	}
    }
  
  return nread;
}  /* End of dl_recvdata() */


/***************************************************************************
 * dl_recvheader:
 *
 * Receive a DataLink packet header composed of two sequence bytes
 * ("DL"), followed a header data length byte, followed by a header
 * body.
 *
 * The header body will be returned in buffer and will always be NULL
 * terminated.  The buffer must be at least 255 bytes in size.  The
 * maximum header length is effectively 254 bytes.
 *
 * Return number of bytes read on success (0 when no data available on
 * non-blocking socket), -1 on connection shutdown and -2 on error.
 ***************************************************************************/
int
dl_recvheader (DLCP *dlconn, void *buffer, size_t buflen, uint8_t blockflag)
{
  int bytesread = 0;
  int headerlen;
  char *cbuffer = buffer;
  
  if ( ! dlconn || ! buffer )
    {
      return -2;
    }
  
  if ( buflen < 255 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_recvheader(): buffer length to small (%zd)\n",
		dlconn->addr, buflen);
      return -2;
    }
  
  /* Receive synchronization bytes and header length */
  if ( (bytesread = dl_recvdata (dlconn, buffer, 3, blockflag)) != 3 )
    {
      /* Bytes read but not 3 is an error */
      if ( bytesread > 0 )
	return -2;
      else
	return bytesread;
    }
  
  /* Test synchronization bytes */
  if ( cbuffer[0] != 'D' || cbuffer[1] != 'L' )
    {
      dl_log_r (dlconn, 2, 0, "[%s] No DataLink packet detected\n",
		dlconn->addr);
      return -2;
    }
  
  /* 3rd byte is the header length */
  headerlen = (uint8_t) cbuffer[2];
  
  /* Receive header payload blocking until completely received */
  if ( (bytesread = dl_recvdata (dlconn, buffer, headerlen, 1)) != headerlen )
    {
      /* Bytes read but not headerlen is an error */
      if ( bytesread > 0 )
	return -2;
      else
	return bytesread;
    }
  
  /* Make sure reply is NULL terminated */
  if ( bytesread == buflen )
    cbuffer[bytesread-1] = '\0';
  else
    cbuffer[bytesread] = '\0';
  
  return bytesread;
}  /* End of dl_recvheader() */
