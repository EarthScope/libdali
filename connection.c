/***************************************************************************
 * connection.c
 *
 * Routines for managing a connection with a DataLink server
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * modified: 2008.052
 ***************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "libdali.h"
#include "portable.h"

/***************************************************************************
 * dl_newdlcp:
 *
 * Allocate, initialze and return a pointer to a new DLCP struct.
 *
 * Returns allocated DLCP struct on success, NULL on error.
 ***************************************************************************/
DLCP *
dl_newdlcp (void)
{
  DLCP *dlconn;

  dlconn = (DLCP *) malloc (sizeof(DLCP));
  
  if ( dlconn == NULL )
    {
      dl_log_r (NULL, 2, 0, "dl_newdlcp(): error allocating memory\n");
      return NULL;
    }
  
  /* Set defaults */
  dlconn->addr         = 0;
  dlconn->clientid     = 0;
  dlconn->keepalive    = 600;
  dlconn->link         = -1;
  dlconn->serverproto  = 0.0;
  dlconn->pktid        = 0;
  dlconn->pkttime      = 0;
  dlconn->keepalive_trig = -1;
  dlconn->keepalive_time = 0.0;
  dlconn->terminate    = 0;
  dlconn->streaming    = 0;
  
  dlconn->log = NULL;
  
  return dlconn;
}  /* End of dl_newdlcp() */


/***************************************************************************
 * dl_freedlcp:
 *
 * Free all memory associated with a DLCP struct.
 *
 ***************************************************************************/
void
dl_freedlcp (DLCP *dlconn)
{
  if ( dlconn->addr )
    free (dlconn->addr);
  
  if ( dlconn->log )
    free (dlconn->log);
  
  free (dlconn);
}  /* End of dl_freedlcp() */


/***************************************************************************
 * dl_getid:
 *
 * Send the ID command including the client ID and optionally parse
 * the capability flags from the server response.
 *
 * Returns -1 on errors, 0 on success.
 ***************************************************************************/
int
dl_getid (DLCP *dlconn, int parseresp)
{
  int ret = 0;
  char sendstr[255];		/* Buffer for command strings */
  char recvstr[255];		/* Buffer for server response */
  char *capptr;                 /* Pointer to capabilities flags */  

  if ( ! dlconn )
    return -1;
  
  /* Sanity check that connection is not in streaming mode */
  if ( dlconn->streaming )
    {
      dl_log_r (dlconn, 1, 1, "[%s] dl_getid(): Connection in streaming mode, cannot continue\n",
		dlconn->addr);
      return -1;
    }
  
  /* Send ID command including client ID */
  snprintf (sendstr, sizeof(sendstr), "ID %s",
	    (dlconn->clientid) ? dlconn->clientid : "");
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
                "[%s] dl_getid(): Unrecognized server ID: %8.8s\n",
                dlconn->addr, recvstr);
      return -1;
    }
  
  /* Parse the response from the server if requested */
  if ( parseresp )
    {
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
	      /* Parse protocol version as a float */
	      ret = sscanf (tptr, "DLPROTO:%f", &dlconn->serverproto);
	      
	      if ( ret != 1 )
		dl_log_r (dlconn, 1, 1,
			  "[%s] dl_getid(): could not parse protocol version from DLPROTO flag: %s\n",
			  dlconn->addr, tptr);
	    }
	}
    }

  return 0;
}  /* End of dl_getid() */


/***************************************************************************
 * dl_position:
 *
 * Position client read position based on a packet ID and packet time.
 *
 * Returns a positive packet ID on success and -1 on error.
 ***************************************************************************/
int64_t
dl_position (DLCP *dlconn, int64_t pktid, dltime_t pkttime)
{
  int64_t replyvalue = 0;
  char reply[255];
  char header[255];
  int headerlen;
  int replylen;
  int rv;
  
  if ( ! dlconn )
    return -1;
  
  if ( dlconn->link <= 0 )
    return -1;
  
  if ( pktid < 0 )
    return -1;
  
  /* Sanity check that connection is not in streaming mode */
  if ( dlconn->streaming )
    {
      dl_log_r (dlconn, 1, 1, "[%s] dl_position(): Connection in streaming mode, cannot continue\n",
		dlconn->addr);
      return -1;
    }
  
  /* Create packet header with command: "POSITION SET pktid pkttime" */
  headerlen = snprintf (header, sizeof(header), "POSITION SET %lld %lld",
			pktid, pkttime);
  
  /* Send command to server */
  replylen = dl_sendpacket (dlconn, header, headerlen, NULL, 0,
			    reply, sizeof(reply));
  
  if ( replylen <= 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_position(): problem sending POSITION command\n",
		dlconn->addr);
      return -1;
    }
  
  /* Reply message, if sent, will be placed into the reply buffer */
  rv = dl_handlereply (dlconn, reply, sizeof(reply), &replyvalue);
  
  /* Log server reply message */
  if ( rv >= 0 )
    dl_log_r (dlconn, 1, 1, "%s\n", reply);
  
  return ( rv < 0 ) ? -1 : replyvalue;
}  /* End of dl_position() */


/***************************************************************************
 * dl_position_after:
 *
 * Position client read position based on a packet data time.
 *
 * Returns a positive packet ID on success and -1 on error.
 ***************************************************************************/
int64_t
dl_position_after (DLCP *dlconn, dltime_t datatime)
{
  int64_t replyvalue = 0;
  char reply[255];
  char header[255];
  int headerlen;
  int replylen;
  int rv;
  
  if ( ! dlconn )
    return -1;
  
  if ( dlconn->link <= 0 )
    return -1;
  
  /* Sanity check that connection is not in streaming mode */
  if ( dlconn->streaming )
    {
      dl_log_r (dlconn, 1, 1, "[%s] dl_position_after(): Connection in streaming mode, cannot continue\n",
		dlconn->addr);
      return -1;
    }
  
  /* Create packet header with command: "POSITION AFTER datatime" */
  headerlen = snprintf (header, sizeof(header), "POSITION AFTER %lld",
			datatime);
  
  /* Send command to server */
  replylen = dl_sendpacket (dlconn, header, headerlen, NULL, 0,
			    reply, sizeof(reply));
  
  if ( replylen <= 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_position_after(): problem sending POSITION command\n",
		dlconn->addr);
      return -1;
    }
  
  /* Reply message, if sent, will be placed into the reply buffer */
  rv = dl_handlereply (dlconn, reply, sizeof(reply), &replyvalue);
  
  /* Log server reply message */
  if ( rv >= 0 )
    dl_log_r (dlconn, 1, 1, "%s\n", reply);
  
  return ( rv < 0 ) ? -1 : replyvalue;
}  /* End of dl_position_after() */


/***************************************************************************
 * dl_match:
 *
 * Send new match pattern to server or reset matching.  If the
 * matchpattern is NULL a zero length pattern command is sent to the
 * server which resets the client matchion setting.
 *
 * Returns the count of currently matched streams on success and -1
 * on error.
 ***************************************************************************/
int64_t
dl_match (DLCP *dlconn, char *matchpattern)
{
  int64_t replyvalue = 0;
  char reply[255];
  char header[255];
  long int patternlen;
  int headerlen;
  int replylen;
  int rv;
  
  if ( ! dlconn )
    return -1;
  
  if ( dlconn->link <= 0 )
    return -1;
  
  /* Sanity check that connection is not in streaming mode */
  if ( dlconn->streaming )
    {
      dl_log_r (dlconn, 1, 1, "[%s] dl_match(): Connection in streaming mode, cannot continue\n",
		dlconn->addr);
      return -1;
    }
  
  patternlen = ( matchpattern ) ? strlen(matchpattern) : 0;
  
  /* Create packet header with command: "MATCH size" */
  headerlen = snprintf (header, sizeof(header), "MATCH %ld",
			patternlen);
  
  /* Send command and pattern to server */
  replylen = dl_sendpacket (dlconn, header, headerlen,
			    matchpattern, patternlen,
			    reply, sizeof(reply));
  
  if ( replylen <= 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_match(): problem sending MATCH command\n",
		dlconn->addr);
      return -1;
    }
  
  /* Reply message, if sent, will be placed into the reply buffer */
  rv = dl_handlereply (dlconn, reply, sizeof(reply), &replyvalue);
  
  /* Log server reply message */
  if ( rv >= 0 )
    dl_log_r (dlconn, 1, 1, "%s\n", reply);
  
  return ( rv < 0 ) ? -1 : replyvalue;
}  /* End of dl_match() */


/***************************************************************************
 * dl_reject:
 *
 * Send new reject pattern to server or reset rejecting.  If the
 * rejectpattern is NULL a zero length pattern command is sent to the
 * server which resets the client rejection setting.
 *
 * Returns the count of currently rejected streams on success and -1
 * on error.
 ***************************************************************************/
int64_t
dl_reject (DLCP *dlconn, char *rejectpattern)
{
  int64_t replyvalue = 0;
  char reply[255];
  char header[255];
  long int patternlen;
  int headerlen;
  int replylen;
  int rv;
  
  if ( ! dlconn )
    return -1;
  
  if ( dlconn->link <= 0 )
    return -1;
  
  /* Sanity check that connection is not in streaming mode */
  if ( dlconn->streaming )
    {
      dl_log_r (dlconn, 1, 1, "[%s] dl_reject(): Connection in streaming mode, cannot continue\n",
		dlconn->addr);
      return -1;
    }

  patternlen = ( rejectpattern ) ? strlen(rejectpattern) : 0;
  
  /* Create packet header with command: "REJECT size" */
  headerlen = snprintf (header, sizeof(header), "REJECT %ld",
			patternlen);
  
  /* Send command and pattern to server */
  replylen = dl_sendpacket (dlconn, header, headerlen,
			    rejectpattern, patternlen,
			    reply, sizeof(reply));
  
  if ( replylen <= 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_reject(): problem sending REJECT command\n",
		dlconn->addr);
      return -1;
    }
  
  /* Reply message, if sent, will be placed into the reply buffer */
  rv = dl_handlereply (dlconn, reply, sizeof(reply), &replyvalue);
  
  /* Log server reply message */
  if ( rv >= 0 )
    dl_log_r (dlconn, 1, 1, "%s\n", reply);
  
  return ( rv < 0 ) ? -1 : replyvalue;
}  /* End of dl_reject() */


/***************************************************************************
 * dl_write:
 *
 * Send a packet to the server and optionally request and process an
 * acknowledgement from the server.
 *
 * Returns -1 on error and 0 on success when no acknowledgement is
 * requested and a positive packet ID on success when acknowledgement
 * is requested.
 ***************************************************************************/
int64_t
dl_write (DLCP *dlconn, void *packet, int packetlen, char *streamid,
	  dltime_t datatime, int ack)
{
  int64_t replyvalue = 0;
  char reply[255];
  char header[255];
  char *flags = ( ack ) ? "A" : "N";
  int headerlen;
  int replylen;
  int rv;
  
  if ( ! dlconn || ! packet || ! streamid )
    return -1;
  
  if ( dlconn->link <= 0 )
    return -1;

  /* Sanity check that connection is not in streaming mode */
  if ( dlconn->streaming )
    {
      dl_log_r (dlconn, 1, 1, "[%s] dl_write(): Connection in streaming mode, cannot continue\n",
		dlconn->addr);
      return -1;
    }
  
  /* Create packet header with command: "WRITE streamid hpdatatime flags size" */
  headerlen = snprintf (header, sizeof(header),
			"WRITE %s %lld %s %d",
			streamid, datatime, flags, packetlen);
  
  /* Send command and packet to server */
  replylen = dl_sendpacket (dlconn, header, headerlen,
			    packet, packetlen,
			    (ack)?reply:NULL, (ack)?sizeof(reply):0);
  
  if ( replylen < 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_write(): problem sending WRITE command\n",
		dlconn->addr);
      return -1;
    }
  else if ( replylen > 0 )
    {
      /* Reply message, if sent, will be placed into the reply buffer */
      rv = dl_handlereply (dlconn, reply, sizeof(reply), &replyvalue);
      
      /* Log server reply message */
      if ( rv >= 0 )
	dl_log_r (dlconn, 1, 3, "%s\n", reply);
      else
	replyvalue = -1;
    }
  
  return replyvalue;
}  /* End of dl_write() */


/***************************************************************************
 * dl_read:
 *
 * Receive a packet (header and data) optionally requesting a specific
 * packet if pktid > 0.
 *
 * Returns 0 on success and -1 on error.
 ***************************************************************************/
int
dl_read (DLCP *dlconn, int64_t pktid, DLPacket *packet, void *packetdata,
	 size_t maxdatasize)
{
  char header[255];
  int headerlen;
  int rv = 0;
  
  if ( ! dlconn || ! packet || ! packetdata )
    return -1;
  
  if ( dlconn->link <= 0 )
    return -1;
  
  /* Sanity check that connection is not in streaming mode */
  if ( dlconn->streaming )
    {
      dl_log_r (dlconn, 1, 1, "[%s] dl_read(): Connection in streaming mode, cannot continue\n",
		dlconn->addr);
      return -1;
    }
  
  /* Request a specific packet */
  if ( pktid > 0 )
    {
      /* Create packet header with command: "READ pktid" */
      headerlen = snprintf (header, sizeof(header), "READ %lld", pktid);
      
      /* Send command and packet to server */
      if ( dl_sendpacket (dlconn, header, headerlen, NULL, 0, NULL, 0) < 0 )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] dl_read(): problem sending READ command\n",
		    dlconn->addr);
	  return -1;
	}
    }
  
  /* Receive packet header, blocking until received */
  if ( (rv = dl_recvheader (dlconn, header, sizeof(header), 1)) < 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_read(): problem receving packet header\n",
		dlconn->addr);
      return -1;
    }
  
  if ( ! strncmp (header, "PACKET", 6) )
    {
      /* Parse PACKET header */
      rv = sscanf (header, "PACKET %s %lld %lld %lld %d",
		   packet->streamid, &(packet->pktid), &(packet->pkttime),
		   &(packet->datatime), &(packet->datasize));
      
      if ( rv != 5 )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] dl_read(): cannot parse PACKET header\n",
		    dlconn->addr);
	  return -1;
	}
      
      if ( packet->datasize > maxdatasize )
	{
	  dl_log_r (dlconn, 2, 0,
		    "[%s] dl_read(): packet data larger (%ld) than receiving buffer (%ld)\n",
		    dlconn->addr, packet->datasize, maxdatasize);
	  return -1;
	}
      
      /* Receive packet data, blocking until complete */
      if ( dl_recvdata (dlconn, packetdata, packet->datasize, 1) != packet->datasize )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] dl_read(): problem receiving packet data\n",
		    dlconn->addr);
	  return -1;
	}
      
      /* Update most recently received packet ID and time */
      dlconn->pktid = packet->pktid;
      dlconn->pkttime = packet->pkttime;
    }
  else if ( ! strncmp (header, "ERROR", 5) )
    {
      /* Reply message, if sent, will be placed into the header buffer */
      rv = dl_handlereply (dlconn, header, sizeof(header), NULL);
      
      /* Log server reply message */
      if ( rv >= 0 )
	dl_log_r (dlconn, 2, 0, "%s\n", header);
      
      return -1;
    }
  else
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_read(): Unrecognized reply string %.6s\n",
		dlconn->addr, header);
      return -1;
    }
  
  return 0;
}  /* End of dl_read() */


/***************************************************************************
 * dl_getinfo:
 *
 * Request and receive information from the server using the INFO command.
 *
 * Returns the lengh of the INFO response on success and -1 on error.
 ***************************************************************************/
int
dl_getinfo (DLCP *dlconn, const char *infotype, void *infodata,
	    size_t maxinfosize)
{
  char header[255];
  char type[255];
  int headerlen;
  int infosize = 0;
  int rv = 0;
  
  if ( ! dlconn || ! infotype || ! infodata )
    return -1;
  
  if ( dlconn->link <= 0 )
    return -1;
  
  /* Sanity check that connection is not in streaming mode */
  if ( dlconn->streaming )
    {
      dl_log_r (dlconn, 1, 1, "[%s] dl_getinfo(): Connection in streaming mode, cannot continue\n",
		dlconn->addr);
      return -1;
    }
  
  /* Request information */
  /* Create packet header with command: "INFO type" */
  headerlen = snprintf (header, sizeof(header), "INFO %s", infotype);
  
  /* Send command and packet to server */
  if ( dl_sendpacket (dlconn, header, headerlen, NULL, 0, NULL, 0) < 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_getinfo(): problem sending INFO command\n",
		dlconn->addr);
      return -1;
    }
  
  /* Receive packet header, blocking until complete: INFO <size> */
  if ( (rv = dl_recvheader (dlconn, header, sizeof(header), 1)) < 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_getinfo(): problem receving packet header\n",
		dlconn->addr);
      return -1;
    }
  
  if ( ! strncmp (header, "INFO", 4) )
    {
      /* Parse INFO header */
      rv = sscanf (header, "INFO %s %d", type, &infosize);
      
      if ( rv != 2 )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] dl_getinfo(): cannot parse INFO header\n",
		    dlconn->addr);
	  return -1;
	}
      
      if ( strncasecmp (infotype, type, strlen(infotype)) )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] dl_getinfo(): requested type %s but received type %s\n",
		    dlconn->addr, infotype, type);
	  return -1;
	}
      
      if ( infosize > maxinfosize )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] dl_getinfo(): INFO data larger (%d) than receiving buffer (%d)\n",
		    dlconn->addr, infosize, maxinfosize);
	  return -1;
	}
      
      /* Receive INFO data, blocking until complete */
      if ( dl_recvdata (dlconn, infodata, infosize, 1) != infosize )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] dl_getinfo(): problem receiving INFO data\n",
		    dlconn->addr);
	  return -1;
	}
    }
  else if ( ! strncmp (header, "ERROR", 5) )
    {
      /* Reply message, if sent, will be placed into the header buffer */
      rv = dl_handlereply (dlconn, header, sizeof(header), NULL);
      
      /* Log server reply message */
      if ( rv >= 0 )
	dl_log_r (dlconn, 2, 0, "%s\n", header);
      
      return -1;
    }
  else
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_getinfo(): Unrecognized reply string %.6s\n",
		dlconn->addr, header);
      return -1;
    }
  
  return infosize;
}  /* End of dl_getinfo() */


/***************************************************************************
 * dl_collect:
 *
 * Send the STREAM command and collect packets sent by the server.
 * Keep alive packets are sent to the server based on the
 * DLCP.keepalive parameter.  If the endflag is true send the
 * ENDSTREAM command.
 *
 * Designed to run in a tight loop at the heart of a client program,
 * this function will return every time a packet is received.  On
 * successfully receiving a packet dlpack will be populated and the
 * packet body will be copied into packet buffer.
 *
 * Returns DLPACKET when a packet is received.  Returns DLENDED when
 * the stream ending sequence was completed or the terminate flag was
 * set.  Returns DLERROR when an error occurred.
 ***************************************************************************/
int
dl_collect (DLCP *dlconn, DLPacket *packet, void *packetdata,
	    size_t maxdatasize, int8_t endflag)
{
  dltime_t now;
  char header[255];
  int  headerlen;
  int  rv;
  
  /* For select()ing during the read loop */
  struct timeval select_tv;
  fd_set         select_fd;
  int            select_ret;
  
  if ( ! dlconn || ! packet || ! packetdata )
    return DLERROR;
  
  if ( dlconn->link == -1 )
    return DLERROR;
  
  /* If not streaming send the STREAM command */
  if ( ! dlconn->streaming && ! endflag )
    {
      /* Create packet header with command: "STREAM" */
      headerlen = snprintf (header, sizeof(header), "STREAM");
      
      /* Send command to server */
      if ( dl_sendpacket (dlconn, header, headerlen, NULL, 0, NULL, 0) < 0 )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] dl_collect(): problem sending STREAM command\n",
		    dlconn->addr);
	  return DLERROR;
	}
      
      dlconn->streaming = 1;
      dlconn->keepalive_trig = -1;
      dl_log_r (dlconn, 1, 2, "[%s] STREAM command sent to server", dlconn->addr);
    }
  
  /* If streaming and end is requested send the ENDSTREAM command */
  if ( dlconn->streaming == 1 && endflag )
    {
      /* Create packet header with command: "ENDSTREAM" */
      headerlen = snprintf (header, sizeof(header), "ENDSTREAM");
      
      /* Send command to server */
      if ( dl_sendpacket (dlconn, header, headerlen, NULL, 0, NULL, 0) < 0 )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] dl_collect(): problem sending ENDSTREAM command\n",
		    dlconn->addr);
	  return DLERROR;
	}
      
      dlconn->streaming = -1;
      dlconn->keepalive_trig = -1;
      dl_log_r (dlconn, 1, 2, "[%s] ENDSTREAM command sent to server", dlconn->addr);
    }
  
  /* Start the primary loop */
  while ( ! dlconn->terminate )
    {
      /* Check if a keepalive packet needs to be sent */
      if ( dlconn->keepalive && dlconn->keepalive_trig > 0 )
	{
	  dl_log_r (dlconn, 1, 2, "[%s] Sending keepalive packet\n", dlconn->addr);
	  
	  /* Send ID as a keepalive packet exchange */
	  headerlen = snprintf (header, sizeof(header), "ID %s",
				(dlconn->clientid) ? dlconn->clientid : "");
	  
	  if ( dl_sendpacket (dlconn, header, headerlen,
			      NULL, 0, NULL, 0) < 0 )
	    {
	      dl_log_r (dlconn, 2, 0, "[%s] dl_collect(): problem sending keepalive packet\n",
			dlconn->addr);
	      return DLERROR;
	    }
	  
	  dlconn->keepalive_trig = -1;
	}
      
      /* Poll the socket for available data */
      FD_ZERO (&select_fd);
      FD_SET ((unsigned int)dlconn->link, &select_fd);
      select_tv.tv_sec = 0;
      select_tv.tv_usec = 500000;  /* Block up to 0.5 seconds */
      
      select_ret = select ((dlconn->link + 1), &select_fd, NULL, NULL, &select_tv);
      
      /* Check the return from select(), an interrupted system call error
	 will be reported if a signal handler was used.  If the terminate
	 flag is set this is not an error. */
      if ( select_ret > 0 )
	{
	  if ( ! FD_ISSET (dlconn->link, &select_fd) )
	    {
	      dl_log_r (dlconn, 2, 0, "[%s] select() reported data but socket not in set!\n",
			dlconn->addr);
	    }
	  else
	    {
	      /* Receive packet header, blocking until complete */
	      if ( (rv = dl_recvheader (dlconn, header, sizeof(header), 1)) < 0 )
		{
		  dl_log_r (dlconn, 2, 0, "[%s] dl_collect(): problem receving packet header\n",
			    dlconn->addr);
		  return DLERROR;
		}
	      
	      if ( ! strncmp (header, "PACKET", 6) )
		{
		  /* Parse PACKET header */
		  rv = sscanf (header, "PACKET %s %lld %lld %lld %d",
			       packet->streamid, &(packet->pktid), &(packet->pkttime),
			       &(packet->datatime), &(packet->datasize));
		  
		  if ( rv != 5 )
		    {
		      dl_log_r (dlconn, 2, 0, "[%s] dl_collect(): cannot parse PACKET header\n",
				dlconn->addr);
		      return DLERROR;
		    }
		  
		  if ( packet->datasize > maxdatasize )
		    {
		      dl_log_r (dlconn, 2, 0,
				"[%s] dl_collect(): packet data larger (%ld) than receiving buffer (%ld)\n",
				dlconn->addr, packet->datasize, maxdatasize);
		      return DLERROR;
		    }
		  
		  /* Receive packet data, blocking until complete */
		  if ( dl_recvdata (dlconn, packetdata, packet->datasize, 1) != packet->datasize )
		    {
		      dl_log_r (dlconn, 2, 0, "[%s] dl_collect(): problem receiving packet data\n",
				dlconn->addr);
		      return DLERROR;
		    }
		  
		  /* Update most recently received packet ID and time */
		  dlconn->pktid = packet->pktid;
		  dlconn->pkttime = packet->pkttime;
		  
		  return DLPACKET;
		}
	      else if ( ! strncmp (header, "ID", 2) )
		{
		  dl_log_r (dlconn, 1, 2, "[%s] Received keepalive (ID) from server\n",
			    dlconn->addr);
		}
	      else if ( ! strncmp (header, "ENDSTREAM", 9) )
		{
		  dl_log_r (dlconn, 1, 2, "[%s] Received end-of-stream from server\n",
			    dlconn->addr);
		  dlconn->streaming = 0;
		  return DLENDED;
		}
	      else
		{
		  dl_log_r (dlconn, 2, 0, "[%s] dl_collect(): Unrecognized packet header %.6s\n",
			    dlconn->addr, header);
		  return DLERROR;
		}
	      
	      dlconn->keepalive_trig = -1;
	    }
	}
      else if ( select_ret < 0 && ! dlconn->terminate )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] select() error: %s\n", dlconn->addr, dlp_strerror ());
	  return DLERROR;
	}
      
      /* Update timing variables */
      now = dlp_time ();
      
      /* Keepalive/heartbeat interval timing logic */
      if ( dlconn->keepalive )
	{
	  if (dlconn->keepalive_trig == -1)  /* reset timer */
	    {
	      dlconn->keepalive_time = now;
	      dlconn->keepalive_trig = 0;
	    }
	  else if (dlconn->keepalive_trig == 0 &&
		   (now - dlconn->keepalive_time) > dlconn->keepalive)
	    {
	      dlconn->keepalive_trig = 1;
	    }
	}
    }  /* End of primary loop */
  
  return DLENDED;
}  /* End of dl_collect() */


/***************************************************************************
 * dl_collect_nb:
 *
 * Send the STREAM command and collect packets sent by the server.
 * Keep alive packets are sent to the server based on the
 * DLCP.keepalive parameter.  If the endflag is true send the
 * ENDSTREAM command.
 *
 * Designed to run in a tight loop at the heart of a client program,
 * this function is a non-blocking version of dl_collect().  On
 * successfully receiving a packet dlpack will be populated and the
 * packet body will be copied into packet buffer.
 *
 * Returns DLPACKET when a packet is received and DLNOPACKET when no
 * packet is received.  Returns DLENDED when the stream ending
 * sequence was completed.  Returns DLERROR when an error occurred.
 ***************************************************************************/
int
dl_collect_nb (DLCP *dlconn, DLPacket *packet, void *packetdata,
	       size_t maxdatasize, int8_t endflag)
{
  dltime_t now;
  char header[255];
  int  headerlen;
  int  rv;
  
  if ( ! dlconn || ! packet || ! packetdata )
    return DLERROR;
  
  if ( dlconn->link == -1 )
    return DLERROR;
  
  /* If not streaming send the STREAM command */
  if ( ! dlconn->streaming && ! endflag )
    {
      /* Create packet header with command: "STREAM" */
      headerlen = snprintf (header, sizeof(header), "STREAM");
      
      /* Send command to server */
      if ( dl_sendpacket (dlconn, header, headerlen, NULL, 0, NULL, 0) < 0 )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] dl_collect_nb(): problem sending STREAM command\n",
		    dlconn->addr);
	  return DLERROR;
	}
      
      dlconn->streaming = 1;
      dlconn->keepalive_trig = -1;
      dl_log_r (dlconn, 1, 2, "[%s] STREAM command sent to server", dlconn->addr);
    }
  
  /* If streaming and end is requested send the ENDSTREAM command */
  if ( dlconn->streaming == 1 && endflag )
    {
      /* Create packet header with command: "ENDSTREAM" */
      headerlen = snprintf (header, sizeof(header), "ENDSTREAM");
      
      /* Send command to server */
      if ( dl_sendpacket (dlconn, header, headerlen, NULL, 0, NULL, 0) < 0 )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] dl_collect_nb(): problem sending ENDSTREAM command\n",
		    dlconn->addr);
	  return DLERROR;
	}
      
      dlconn->streaming = -1;
      dlconn->keepalive_trig = -1;
      dl_log_r (dlconn, 1, 2, "[%s] ENDSTREAM command sent to server", dlconn->addr);
    }
  
  if ( ! dlconn->terminate )
    {
      /* Check if a keepalive packet needs to be sent */
      if ( dlconn->keepalive && dlconn->keepalive_trig > 0 )
	{
	  dl_log_r (dlconn, 1, 2, "[%s] Sending keepalive packet\n", dlconn->addr);
	  
	  /* Send ID as a keepalive packet exchange */
	  headerlen = snprintf (header, sizeof(header), "ID %s",
				(dlconn->clientid) ? dlconn->clientid : "");
	  
	  if ( dl_sendpacket (dlconn, header, headerlen,
			      NULL, 0, NULL, 0) < 0 )
	    {
	      dl_log_r (dlconn, 2, 0, "[%s] dl_collect_nb(): problem sending keepalive packet\n",
			dlconn->addr);
	      return DLERROR;
	    }
	  
	  dlconn->keepalive_trig = -1;
	}
    }
  
  /* Receive packet header if it's available */
  if ( (rv = dl_recvheader (dlconn, header, sizeof(header), 0)) < 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_collect_nb(): problem receving packet header\n",
		dlconn->addr);
      return DLERROR;
    }
  
  /* Process data if header received */
  if ( rv > 0 )
    {
      if ( ! strncmp (header, "PACKET", 6) )
	{
	  /* Parse PACKET header */
	  rv = sscanf (header, "PACKET %s %lld %lld %lld %d",
		       packet->streamid, &(packet->pktid), &(packet->pkttime),
		       &(packet->datatime), &(packet->datasize));
	  
	  if ( rv != 5 )
	    {
	      dl_log_r (dlconn, 2, 0, "[%s] dl_collect_nb(): cannot parse PACKET header\n",
			dlconn->addr);
	      return DLERROR;
	    }
	  
	  if ( packet->datasize > maxdatasize )
	    {
	      dl_log_r (dlconn, 2, 0,
			"[%s] dl_collect_nb(): packet data larger (%ld) than receiving buffer (%ld)\n",
			dlconn->addr, packet->datasize, maxdatasize);
	      return DLERROR;
	    }
	  
	  /* Receive packet data, blocking until complete */
	  if ( dl_recvdata (dlconn, packetdata, packet->datasize, 1) != packet->datasize )
	    {
	      dl_log_r (dlconn, 2, 0, "[%s] dl_collect_nb(): problem receiving packet data\n",
			dlconn->addr);
	      return DLERROR;
	    }
	  
	  /* Update most recently received packet ID and time */
	  dlconn->pktid = packet->pktid;
	  dlconn->pkttime = packet->pkttime;
	  
	  return DLPACKET;
	}
      else if ( ! strncmp (header, "ID", 2) )
	{
	  dl_log_r (dlconn, 1, 2, "[%s] Received keepalive (ID) from server\n", dlconn->addr);
	}
      else if ( ! strncmp (header, "ENDSTREAM", 9) )
	{
	  dl_log_r (dlconn, 1, 2, "[%s] Received end-of-stream from server\n", dlconn->addr);
	  dlconn->streaming = 0;
	  return DLENDED;
	}
      else
	{
	  dl_log_r (dlconn, 2, 0, "[%s] dl_collect_nb(): Unrecognized packet header %.6s\n",
		    dlconn->addr, header);
	  return DLERROR;
	}
      
      dlconn->keepalive_trig = -1;
    }
  
  /* Update timing variables */
  now = dlp_time ();
  
  /* Keepalive/heartbeat interval timing logic */
  if ( dlconn->keepalive )
    {
      if ( dlconn->keepalive_trig == -1 )  /* reset timer */
	{
	  dlconn->keepalive_time = now;
	  dlconn->keepalive_trig = 0;
	}
      else if ( dlconn->keepalive_trig == 0 &&
		(now - dlconn->keepalive_time) > dlconn->keepalive )
	{
	  dlconn->keepalive_trig = 1;
	}
    }
  
  return DLNOPACKET;
}  /* End of dl_collect_nb() */


/***************************************************************************
 * dl_handlereply:
 *
 * Handle a servers reply to a command.  Server replies are of the form:
 *
 * "OK|ERROR value size"
 *
 * followed by an optional server message of size bytes.  If size is
 * greater than zero it will be read from the connection and placed
 * into buffer.  The server message, if included, will always be a
 * NULL-terminated string.
 *
 * Return values:
 * -1 = error
 *  0 = "OK" received
 *  1 = "ERROR" received
 ***************************************************************************/
int
dl_handlereply (DLCP *dlconn, void *buffer, int buflen, int64_t *value)
{
  char status[10];
  char *cbuffer = buffer;
  int64_t pvalue;
  int64_t size = 0;
  int rv = 0;
  
  if ( ! dlconn || ! buffer )
    return -1;
  
  /* Make sure buffer if terminated */
  cbuffer[buflen] = '\0';
  
  /* Parse reply header */
  if ( sscanf (buffer, "%10s %lld %lld", status, &pvalue, &size) != 3 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_handlereply(): Unable to parse reply header: '%s'\n",
		dlconn->addr, buffer);
      return -1;
    }
  
  /* Store reply value if requested */
  if ( value )
    *value = pvalue;
  
  /* Check that reply message will fit into buffer */
  if ( size > buflen )
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_handlereply(): Reply message too large (%d) for buffer (%d)\n",
		dlconn->addr, size, buflen);
      return -1;      
    }
  
  /* Receive reply message if included */
  if ( size > 0 )
    {
      /* Receive reply message, blocking until complete */
      if ( dl_recvdata (dlconn, buffer, size, 1) != size )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] dl_handlereply(): Problem receiving reply message\n",
		    dlconn->addr);
	  return -1;
	}
      
      if ( size < buflen )
	cbuffer[size] = '\0';
      else
	cbuffer[buflen-1] = '\0';
    }
  /* Make sure buffer is terminated */
  else
    {
      cbuffer[0] = '\0';
    }
  
  /* Check for "OK" status in reply header */
  if ( ! strncmp (status, "OK", 2) )
    {
      rv = 0;
    }
  else if ( ! strncmp (status, "ERROR", 5) )
    {
      rv = 1;
    }
  else
    {
      dl_log_r (dlconn, 2, 0, "[%s] dl_handlereply(): Unrecognized reply string %.5s\n",
		dlconn->addr, buffer);
      rv = -1;
    }
  
  return rv;
}  /* End of dl_handlereply() */


/***************************************************************************
 * dl_terminate:
 *
 * Set the terminate flag in the DLCP.
 ***************************************************************************/
void
dl_terminate (DLCP *dlconn)
{
  dl_log_r (dlconn, 1, 1, "[%s] Terminating connection\n", dlconn->addr);
  
  dlconn->terminate = 1;
}  /* End of dl_terminate() */
