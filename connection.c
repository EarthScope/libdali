/***************************************************************************
 * connection.c
 *
 * Routines for managing a connection with a DataLink server
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * modified: 2008.033
 ***************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "libdali.h"


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
  dlconn->terminate    = 0;
  
  dlconn->keepalive    = 0;
  dlconn->netto        = 600;
  dlconn->netdly       = 30;
  
  dlconn->serverproto  = 0.0;
  dlconn->link         = -1;
  
  /* Allocate the associated persistent state struct */
  dlconn->stat = (DLStat *) malloc (sizeof(DLStat));
  
  if ( ! dlconn->stat )
    {
      dl_log_r (NULL, 2, 0, "dl_newdlcp(): error allocating memory\n");
      free (dlconn);
      return NULL;
    }
    
  dlconn->stat->pktid          = 0;
  dlconn->stat->pkttime        = 0;
  
  dlconn->stat->netto_trig     = -1;
  dlconn->stat->netdly_trig    = 0;
  dlconn->stat->keepalive_trig = -1;
  
  dlconn->stat->netto_time     = 0.0;
  dlconn->stat->netdly_time    = 0.0;
  dlconn->stat->keepalive_time = 0.0;
  
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
  
  if ( dlconn->stat )
    free (dlconn->stat);
  
  if ( dlconn->log )
    free (dlconn->log);
  
  free (dlconn);
}  /* End of dl_freedlcp() */


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
  
  /* Create packet header with command: "POSITION SET pktid pkttime" */
  headerlen = snprintf (header, sizeof(header), "POSITION SET %lld %lld",
			pktid, pkttime);
  
  /* Send command to server */
  replylen = dl_sendpacket (dlconn, header, headerlen, NULL, 0,
			    reply, sizeof(reply));
  
  if ( replylen <= 0 )
    {
      dl_log_r (dlconn, 2, 0, "problem sending POSITION command\n");
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
  char pkttime[100];
  char reply[255];
  char header[255];
  int headerlen;
  int replylen;
  int rv;
  
  if ( ! dlconn )
    return -1;
  
  if ( dlconn->link <= 0 )
    return -1;
  
  /* Create packet header with command: "POSITION AFTER datatime" */
  headerlen = snprintf (header, sizeof(header), "POSITION AFTER %lld",
			datatime);
  
  /* Send command to server */
  replylen = dl_sendpacket (dlconn, header, headerlen, NULL, 0,
			    reply, sizeof(reply));
  
  if ( replylen <= 0 )
    {
      dl_log_r (dlconn, 2, 0, "problem sending POSITION command\n");
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
  int patternlen;
  int headerlen;
  int replylen;
  int rv;
  
  if ( ! dlconn )
    return -1;
  
  if ( dlconn->link <= 0 )
    return -1;
  
  patternlen = ( matchpattern ) ? strlen(matchpattern) : 0;
  
  /* Create packet header with command: "MATCH size" */
  headerlen = snprintf (header, sizeof(header), "MATCH %lld",
			patternlen);
  
  /* Send command and pattern to server */
  replylen = dl_sendpacket (dlconn, header, headerlen,
			    matchpattern, patternlen,
			    reply, sizeof(reply));
  
  if ( replylen <= 0 )
    {
      dl_log_r (dlconn, 2, 0, "problem sending MATCH command\n");
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
  int patternlen;
  int headerlen;
  int replylen;
  int rv;
  
  if ( ! dlconn )
    return -1;
  
  if ( dlconn->link <= 0 )
    return -1;
  
  patternlen = ( rejectpattern ) ? strlen(rejectpattern) : 0;
  
  /* Create packet header with command: "REJECT size" */
  headerlen = snprintf (header, sizeof(header), "REJECT %lld",
			patternlen);
  
  /* Send command and pattern to server */
  replylen = dl_sendpacket (dlconn, header, headerlen,
			    rejectpattern, patternlen,
			    reply, sizeof(reply));
  
  if ( replylen <= 0 )
    {
      dl_log_r (dlconn, 2, 0, "problem sending REJECT command\n");
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
      dl_log_r (dlconn, 2, 0, "problem sending WRITE command\n");
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
 * Request and receive a packet from the server.
 *
 * Returns 0 on success and -1 on error.
 ***************************************************************************/
int
dl_read (DLCP *dlconn, int64_t pktid, DLPacket *packet, void *packetdata,
	 int32_t maxdatalen)
{
  char header[255];
  int headerlen;
  int rv = 0;
  
  if ( ! dlconn || ! packet || ! packetdata )
    return -1;
  
  if ( dlconn->link <= 0 )
    return -1;
  
  /* Create packet header with command: "READ pktid" */
  headerlen = snprintf (header, sizeof(header), "READ %lld", pktid);
  
  /* Send command and packet to server */
  if ( dl_sendpacket (dlconn, header, headerlen, NULL, 0, NULL, 0) < 0 )
    {
      dl_log_r (dlconn, 2, 0, "problem sending READ command\n");
      return -1;
    }
  
  /* Receive packet header */
  if ( (rv = dl_recvheader (dlconn, header, sizeof(header))) < 0 )
    {
      dl_log_r (dlconn, 2, 0, "problem receving packet header\n");
      return -1;
    }
  
  if ( ! strncmp (header, "PACKET", 6) )
    {
      /* Parse PACKET header */
      rv = sscanf (header, "PACKET %s %lld %lld %ld",
		   packet->streamid, &(packet->pkttime),
		   &(packet->datatime), &(packet->datasize));
      
      if ( rv != 4 )
	{
	  dl_log_r (dlconn, 2, 0, "cannot parse PACKET header\n");
	  return -1;
	}
      
      if ( packet->datasize > maxdatalen )
	{
	  dl_log_r (dlconn, 2, 0,
		    "packet data larger (%ld) than receiving buffer (%ld)\n",
		    packet->datasize, maxdatalen);
	  return -1;
	}
      
      /* Receive packet data */
      if ( dl_recvdata (dlconn, packetdata, packet->datasize) != packet->datasize )
	{
	  dl_log_r (dlconn, 2, 0, "problem receiving packet data\n");
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
      dl_log_r (dlconn, 2, 0, "Unrecognized reply string %.6s\n", header);
      return -1;
    }
  
  return 0;
}  /* End of dl_read() */


/***************************************************************************
 * dl_collect:
 *
 * Routine to manage a connection to a Datalink server based on the values
 * given in the dlconn struct and collect data.
 *
 * Designed to run in a tight loop at the heart of a client program, this
 * function will return every time a packet is received.
 *
 * Returns DLPACKET when something is received and sets the dlpack
 * pointer to the received packet.  When the connection was closed by
 * the server or the termination sequence completed SLTERMINATE is
 * returned and the dlpack pointer is set to NULL.
 ***************************************************************************/
int
dl_collect (DLCP *dlconn, DLPacket ** dlpack)
{
  int    bytesread;
  double current_time;
  char   retpacket;

  /* For select()ing during the read loop */
  struct timeval select_tv;
  fd_set         select_fd;
  int            select_ret;

  *dlpack = NULL;

  /* Check if the info was set */
  if ( dlconn->info != NULL )
    {
      dlconn->stat->query_mode = InfoQuery;
    }
  
  /* If the connection is not up check the DLCP and reset the timing variables */
  if ( dlconn->link == -1 )
    {
      if ( dl_checkdlcp(dlconn) )
	{
	  dl_log_r (dlconn, 2, 0, "problems with the connection description\n");
	  return SLTERMINATE;
	}

      dlconn->stat->netto_trig     = -1;	   /* Init net timeout trigger to reset state */
      dlconn->stat->keepalive_trig = -1;	   /* Init keepalive trigger to reset state */
    }

  /* Start the primary loop  */
  while (1)
    {
      if ( ! dlconn->terminate )
	{
	  if (dlconn->link == -1)
	    {
	      dlconn->stat->dl_state = DL_DOWN;
	    }
	  
	  /* Check for network timeout */
	  if (dlconn->stat->dl_state == DL_DATA && dlconn->netto && dlconn->stat->netto_trig > 0)
	    {
	      dl_log_r (dlconn, 1, 0, "network timeout (%ds), reconnecting in %ds\n",
			dlconn->netto, dlconn->netdly);
	      dlconn->link = dl_disconnect (dlconn);
	      dlconn->stat->dl_state = DL_DOWN;
	      dlconn->stat->netto_trig = -1;
	      dlconn->stat->netdly_trig = -1;
	    }
	  
	  /* Check if a keepalive packet needs to be sent */
	  if (dlconn->stat->dl_state == DL_DATA && !dlconn->stat->expect_info &&
	      dlconn->keepalive && dlconn->stat->keepalive_trig > 0)
	    {
	      dl_log_r (dlconn, 1, 2, "sending keepalive request\n");
	      
	      if ( dl_send_info (dlconn, "ID", 3) != -1 )
		{
		  dlconn->stat->query_mode = KeepAliveQuery;
		  dlconn->stat->expect_info = 1;
		  dlconn->stat->keepalive_trig = -1;
		}
	    }
	  
	  /* Check if an in-stream INFO request needs to be sent */
	  if (dlconn->stat->dl_state == DL_DATA && !dlconn->stat->expect_info && dlconn->info)
	    {
	      if ( dl_send_info (dlconn, dlconn->info, 1) != -1 )
		{
		  dlconn->stat->query_mode = InfoQuery;
		  dlconn->stat->expect_info = 1;
		}
	      else
		{
		  dlconn->stat->query_mode = NoQuery;
		}
	      
	      dlconn->info = NULL;
	    }
	  
	  /* Throttle the loop while delaying */
	  if (dlconn->stat->dl_state == DL_DOWN && dlconn->stat->netdly_trig > 0)
	    {
	      slp_usleep (500000);
	    }
	  
	  /* Connect to remote Datalink */
	  if (dlconn->stat->dl_state == DL_DOWN && dlconn->stat->netdly_trig == 0)
	    {
	      if (dl_connect (dlconn, 1) != -1)
		{
		  dlconn->stat->dl_state = DL_UP;
		}
	      dlconn->stat->netto_trig = -1;
	      dlconn->stat->netdly_trig = -1;
	      dlconn->stat->keepalive_trig = -1;
	    }
	  
	  /* Negotiate/configure the connection */
	  if (dlconn->stat->dl_state == DL_UP)
	    {
	      int slconfret = 0;
	      
	      /* Only send query if a query is set and no streams are defined,
	       * if streams are defined we'll send the query after configuration.
	       */
	      if (dlconn->info && dlconn->streams == NULL)
		{
		  if ( dl_send_info (dlconn, dlconn->info, 1) != -1 )
		    {
		      dlconn->stat->query_mode = InfoQuery;
		      dlconn->stat->expect_info = 1;
		    }
		  else
		    {
		      dlconn->stat->query_mode = NoQuery;
		      dlconn->stat->expect_info = 0;
		    }
		  
		  dlconn->info = NULL;
		}
	      else
		{
		  slconfret = dl_configlink (dlconn);
		  dlconn->stat->expect_info = 0;
		}
	      
	      if (slconfret != -1)
		{
		  dlconn->stat->recptr = 0;    /* initialize the data buffer pointers */
		  dlconn->stat->sendptr = 0;
		  dlconn->stat->dl_state = DL_DATA;
		}
	      else
		{
		  dl_log_r (dlconn, 2, 0, "negotiation with remote Datalink failed\n");
		  dlconn->link = dl_disconnect (dlconn);
		  dlconn->stat->netdly_trig = -1;
		}
	    }
	}
      else /* We are terminating */
	{
	  if (dlconn->link != -1)
	    {
	      dlconn->link = dl_disconnect (dlconn);
	    }

	  dlconn->stat->dl_state = DL_DOWN;
	}

      /* DEBUG
      dl_log_r (dlconn, 1, 0, "link: %d, sendptr: %d, recptr: %d, diff: %d\n",
                dlconn->link, dlconn->stat->sendptr, dlconn->stat->recptr,
		(dlconn->stat->recptr - dlconn->stat->sendptr) );
      */
	  
      /* Process data in buffer */
      while (dlconn->stat->recptr - dlconn->stat->sendptr >= SLHEADSIZE + SLRECSIZE)
	{
	  retpacket = 1;
	  
	  /* Check for an INFO packet */
	  if (!strncmp (&dlconn->stat->databuf[dlconn->stat->sendptr], INFOSIGNATURE, 6))
	    {
	      char terminator;
	      
	      terminator = (dlconn->stat->databuf[dlconn->stat->sendptr + SLHEADSIZE - 1] != '*');
	      
	      if ( !dlconn->stat->expect_info )
		{
		  dl_log_r (dlconn, 2, 0, "unexpected INFO packet received, skipping\n");
		}
	      else
		{
		  if ( terminator )
		    {
		      dlconn->stat->expect_info = 0;
		    }
		  
		  /* Keep alive packets are not returned */
		  if ( dlconn->stat->query_mode == KeepAliveQuery )
		    {
		      retpacket = 0;
		      
		      if ( !terminator )
			{
			  dl_log_r (dlconn, 2, 0, "non-terminated keep-alive packet received!?!\n");
			}
		      else
			{
			  dl_log_r (dlconn, 1, 2, "keepalive packet received\n");
			}
		    }
		}
	      
	      if ( dlconn->stat->query_mode != NoQuery )
		{
		  dlconn->stat->query_mode = NoQuery;
		}
	    }
	  else    /* Update the stream chain entry if not an INFO packet */
	    {
	      if ( (update_stream (dlconn, (DLPacket *) &dlconn->stat->databuf[dlconn->stat->sendptr])) == -1 )
		{
		  /* If updating didn't work the packet is broken */
		  retpacket = 0;
		}
	    }
	  
	  /* Increment the send pointer */
	  dlconn->stat->sendptr += (SLHEADSIZE + SLRECSIZE);
	  
	  /* Return packet */
	  if ( retpacket )
	    {
	      *dlpack = (DLPacket *) &dlconn->stat->databuf[dlconn->stat->sendptr - (SLHEADSIZE + SLRECSIZE)];
	      return DLPACKET;
	    }
	}
      
      /* A trap door for terminating, all complete data packets from the buffer
	 have been sent to the caller */
      if ( dlconn->terminate ) {
	return SLTERMINATE;
      }
      
      /* After processing the packet buffer shift the data */
      if ( dlconn->stat->sendptr )
	    {
	      memmove (dlconn->stat->databuf,
		       &dlconn->stat->databuf[dlconn->stat->sendptr],
		       dlconn->stat->recptr - dlconn->stat->sendptr);
	      
	      dlconn->stat->recptr -= dlconn->stat->sendptr;
	      dlconn->stat->sendptr = 0;
	    }
      
      /* Catch cases where the data stream stopped */
      if ((dlconn->stat->recptr - dlconn->stat->sendptr) == 7 &&
	  !strncmp (&dlconn->stat->databuf[dlconn->stat->sendptr], "ERROR\r\n", 7))
	{
	  dl_log_r (dlconn, 2, 0, "Datalink server reported an error with the last command\n");
	  dlconn->link = dl_disconnect (dlconn);
	  return SLTERMINATE;
	}
      
      if ((dlconn->stat->recptr - dlconn->stat->sendptr) == 3 &&
	  !strncmp (&dlconn->stat->databuf[dlconn->stat->sendptr], "END", 3))
	{
	  dl_log_r (dlconn, 1, 1, "End of buffer or selected time window\n");
	  dlconn->link = dl_disconnect (dlconn);
	  return SLTERMINATE;
	}
      
      /* Read incoming data if connection is up */
      if (dlconn->stat->dl_state == DL_DATA)
	{
	  /* Check for more available data from the socket */
	  bytesread = 0;

	  /* Poll the server */
	  FD_ZERO (&select_fd);
	  FD_SET ((unsigned int)dlconn->link, &select_fd);
	  select_tv.tv_sec = 0;
	  select_tv.tv_usec = 500000;  /* Block up to 0.5 seconds */

	  select_ret = select ((dlconn->link + 1), &select_fd, NULL, NULL, &select_tv);

	  /* Check the return from select(), an interrupted system call error
	     will be reported if a signal handler was used.  If the terminate
	     flag is set this is not an error. */
	  if (select_ret > 0)
	    {
	      if (!FD_ISSET (dlconn->link, &select_fd))
		{
		  dl_log_r (dlconn, 2, 0, "select() reported data but socket not in set!\n");
		}
	      else
		{
		  bytesread = dl_recvdata (dlconn, (void *) &dlconn->stat->databuf[dlconn->stat->recptr],
					   BUFSIZE - dlconn->stat->recptr, dlconn->sladdr);
		}
	    }
	  else if (select_ret < 0 && ! dlconn->terminate)
	    {
	      dl_log_r (dlconn, 2, 0, "select() error: %s\n", slp_strerror ());
	      dlconn->link = dl_disconnect (dlconn);
	      dlconn->stat->netdly_trig = -1;
	    }

	  if (bytesread < 0)            /* read() failed */
	    {
	      dlconn->link = dl_disconnect (dlconn);
	      dlconn->stat->netdly_trig = -1;
	    }
	  else if (bytesread > 0)	/* Data is here, process it */
	    {
	      dlconn->stat->recptr += bytesread;

	      /* Reset the timeout and keepalive timers */
	      dlconn->stat->netto_trig     = -1;
	      dlconn->stat->keepalive_trig = -1;
	    }
	}

      /* Update timing variables */
      current_time = dl_dtime ();

      /* Network timeout timing logic */
      if (dlconn->netto)
	{
	  if (dlconn->stat->netto_trig == -1)	/* reset timer */
	    {
	      dlconn->stat->netto_time = current_time;
	      dlconn->stat->netto_trig = 0;
	    }
	  else if (dlconn->stat->netto_trig == 0 &&
		   (current_time - dlconn->stat->netto_time) > dlconn->netto)
	    {
	      dlconn->stat->netto_trig = 1;
	    }
	}
      
      /* Keepalive/heartbeat interval timing logic */
      if (dlconn->keepalive)
	{
	  if (dlconn->stat->keepalive_trig == -1)	/* reset timer */
	    {
	      dlconn->stat->keepalive_time = current_time;
	      dlconn->stat->keepalive_trig = 0;
	    }
	  else if (dlconn->stat->keepalive_trig == 0 &&
		   (current_time - dlconn->stat->keepalive_time) > dlconn->keepalive)
	    {
	      dlconn->stat->keepalive_trig = 1;
	    }
	}
      
      /* Network delay timing logic */
      if (dlconn->netdly)
	{
	  if (dlconn->stat->netdly_trig == -1)	/* reset timer */
	    {
	      dlconn->stat->netdly_time = current_time;
	      dlconn->stat->netdly_trig = 1;
	    }
	  else if (dlconn->stat->netdly_trig == 1 &&
		   (current_time - dlconn->stat->netdly_time) > dlconn->netdly)
	    {
	      dlconn->stat->netdly_trig = 0;
	    }
	}
    }				/* End of primary loop */
}  /* End of dl_collect() */


/***************************************************************************
 * dl_collect_nb:
 *
 * Routine to manage a connection to a Datalink server based on the values
 * given in the dlconn struct and collect data.
 *
 * Designed to run in a tight loop at the heart of a client program, this
 * function is a non-blocking version dl_collect().  SLNOPACKET will be
 * returned when no data is available.
 *
 * Returns DLPACKET when something is received and sets the dlpack
 * pointer to the received packet.  Returns SLNOPACKET when no packets
 * have been received, dlpack is set to NULL.  When the connection was
 * closed by the server or the termination sequence completed
 * SLTERMINATE is returned and the dlpack pointer is set to NULL.
 ***************************************************************************/
int
dl_collect_nb (DLCP *dlconn, DLPacket ** dlpack) 
{
  int    bytesread;
  double current_time;
  char   retpacket;

  *dlpack = NULL;

  /* Check if the info was set */
  if ( dlconn->info != NULL )
    {
      dlconn->stat->query_mode = InfoQuery;
    }
  
  /* If the connection is not up check the DLCP and reset the timing variables */
  if ( dlconn->link == -1 )
    {
      if ( dl_checkdlcp(dlconn) )
	{
	  dl_log_r (dlconn, 2, 0, "problems with the connection description\n");
	  return SLTERMINATE;
	}

      dlconn->stat->netto_trig     = -1;	   /* Init net timeout trigger to reset state */
      dlconn->stat->keepalive_trig = -1;	   /* Init keepalive trigger to reset state */
    }

  /* Start the main sequence  */
  if ( ! dlconn->terminate )
    {
      if (dlconn->link == -1)
	{
	  dlconn->stat->dl_state = DL_DOWN;
	}
      
      /* Check for network timeout */
      if (dlconn->stat->dl_state == DL_DATA &&
	  dlconn->netto && dlconn->stat->netto_trig > 0)
	{
	  dl_log_r (dlconn, 1, 0, "network timeout (%ds), reconnecting in %ds\n",
		    dlconn->netto, dlconn->netdly);
	  dlconn->link = dl_disconnect (dlconn);
	  dlconn->stat->dl_state = DL_DOWN;
	  dlconn->stat->netto_trig = -1;
	  dlconn->stat->netdly_trig = -1;
	}
      
      /* Check if a keepalive packet needs to be sent */
      if (dlconn->stat->dl_state == DL_DATA && !dlconn->stat->expect_info &&
	  dlconn->keepalive && dlconn->stat->keepalive_trig > 0)
	{
	  dl_log_r (dlconn, 1, 2, "sending keepalive request\n");
	  
	  if ( dl_send_info (dlconn, "ID", 3) != -1 )
	    {
	      dlconn->stat->query_mode = KeepAliveQuery;
	      dlconn->stat->expect_info = 1;
	      dlconn->stat->keepalive_trig = -1;
	    }
	}
      
      /* Check if an in-stream INFO request needs to be sent */
      if (dlconn->stat->dl_state == DL_DATA &&
	  !dlconn->stat->expect_info && dlconn->info)
	{
	  if ( dl_send_info (dlconn, dlconn->info, 1) != -1 )
	    {
	      dlconn->stat->query_mode = InfoQuery;
	      dlconn->stat->expect_info = 1;
	    }
	  else
	    {
	      dlconn->stat->query_mode = NoQuery;
	    }
	  
	  dlconn->info = NULL;
	}
      
      /* Throttle the loop while delaying */
      if (dlconn->stat->dl_state == DL_DOWN && dlconn->stat->netdly_trig > 0)
	{
	  slp_usleep (500000);
	}
      
      /* Connect to remote Datalink */
      if (dlconn->stat->dl_state == DL_DOWN && dlconn->stat->netdly_trig == 0)
	{
	  if (dl_connect (dlconn, 1) != -1)
	    {
	      dlconn->stat->dl_state = DL_UP;
	    }
	  dlconn->stat->netto_trig = -1;
	  dlconn->stat->netdly_trig = -1;
	  dlconn->stat->keepalive_trig = -1;
	}
      
      /* Negotiate/configure the connection */
      if (dlconn->stat->dl_state == DL_UP)
	{
	  int slconfret = 0;
	  
	  /* Only send query if a query is set and no streams are defined,
	   * if streams are defined we'll send the query after configuration.
	   */
	  if (dlconn->info && dlconn->streams == NULL)
	    {
	      if ( dl_send_info (dlconn, dlconn->info, 1) != -1 )
		{
		  dlconn->stat->query_mode = InfoQuery;
		  dlconn->stat->expect_info = 1;
		}
	      else
		{
		  dlconn->stat->query_mode = NoQuery;
		  dlconn->stat->expect_info = 0;
		}
	      
	      dlconn->info = NULL;
	    }
	  else
	    {
	      slconfret = dl_configlink (dlconn);
	      dlconn->stat->expect_info = 0;
	    }
	  
	  if (slconfret != -1)
	    {
	      dlconn->stat->recptr   = 0;	/* initialize the data buffer pointers */
	      dlconn->stat->sendptr  = 0;
	      dlconn->stat->dl_state = DL_DATA;
	    }
	  else
	    {
	      dl_log_r (dlconn, 2, 0, "negotiation with remote Datalink failed\n");
	      dlconn->link = dl_disconnect (dlconn);
	      dlconn->stat->netdly_trig = -1;
	    }
	}
    }
  else /* We are terminating */
    {
      if (dlconn->link != -1)
	{
	  dlconn->link = dl_disconnect (dlconn);
	}

      dlconn->stat->dl_state = DL_DATA;
    }

  /* DEBUG
     dl_log_r (dlconn, 1, 0, "link: %d, sendptr: %d, recptr: %d, diff: %d\n",
     dlconn->link, dlconn->stat->sendptr, dlconn->stat->recptr,
     (dlconn->stat->recptr - dlconn->stat->sendptr) );
  */
        
  /* Process data in buffer */
  while (dlconn->stat->recptr - dlconn->stat->sendptr >= SLHEADSIZE + SLRECSIZE)
    {
      retpacket = 1;
      
      /* Check for an INFO packet */
      if (!strncmp (&dlconn->stat->databuf[dlconn->stat->sendptr], INFOSIGNATURE, 6))
	{
	  char terminator;
	  
	  terminator = (dlconn->stat->databuf[dlconn->stat->sendptr + SLHEADSIZE - 1] != '*');
	  
	  if ( !dlconn->stat->expect_info )
	    {
	      dl_log_r (dlconn, 2, 0, "unexpected INFO packet received, skipping\n");
	    }
	  else
	    {
	      if ( terminator )
		{
		  dlconn->stat->expect_info = 0;
		}
	      
	      /* Keep alive packets are not returned */
	      if ( dlconn->stat->query_mode == KeepAliveQuery )
		{
		  retpacket = 0;
		  
		  if ( !terminator )
		    {
		      dl_log_r (dlconn, 2, 0, "non-terminated keep-alive packet received!?!\n");
		    }
		  else
		    {
		      dl_log_r (dlconn, 1, 2, "keepalive packet received\n");
		    }
		}
	    }
	  
	  if ( dlconn->stat->query_mode != NoQuery )
	    {
	      dlconn->stat->query_mode = NoQuery;
	    }
	}
      else    /* Update the stream chain entry if not an INFO packet */
	{
	  if ( (update_stream (dlconn, (DLPacket *) &dlconn->stat->databuf[dlconn->stat->sendptr])) == -1 )
	    {
	      /* If updating didn't work the packet is broken */
	      retpacket = 0;
	    }
	}
      
      /* Increment the send pointer */
      dlconn->stat->sendptr += (SLHEADSIZE + SLRECSIZE);
      
      /* Return packet */
      if ( retpacket )
	{
	  *dlpack = (DLPacket *) &dlconn->stat->databuf[dlconn->stat->sendptr - (SLHEADSIZE + SLRECSIZE)];
	  return DLPACKET;
	}
    }
  
  /* A trap door for terminating, all complete data packets from the buffer
     have been sent to the caller */
  if ( dlconn->terminate ) {
    return SLTERMINATE;
  }
  
  /* After processing the packet buffer shift the data */
  if ( dlconn->stat->sendptr )
    {
      memmove (dlconn->stat->databuf,
	       &dlconn->stat->databuf[dlconn->stat->sendptr],
	       dlconn->stat->recptr - dlconn->stat->sendptr);
      
      dlconn->stat->recptr -= dlconn->stat->sendptr;
      dlconn->stat->sendptr = 0;
    }
  
  /* Catch cases where the data stream stopped */
  if ((dlconn->stat->recptr - dlconn->stat->sendptr) == 7 &&
      !strncmp (&dlconn->stat->databuf[dlconn->stat->sendptr], "ERROR\r\n", 7))
    {
      dl_log_r (dlconn, 2, 0, "Datalink server reported an error with the last command\n");
      dlconn->link = dl_disconnect (dlconn);
      return SLTERMINATE;
    }
  
  if ((dlconn->stat->recptr - dlconn->stat->sendptr) == 3 &&
      !strncmp (&dlconn->stat->databuf[dlconn->stat->sendptr], "END", 3))
    {
      dl_log_r (dlconn, 1, 1, "End of buffer or selected time window\n");
      dlconn->link = dl_disconnect (dlconn);
      return SLTERMINATE;
    }
  
  /* Process data in our buffer and then read incoming data */
  if (dlconn->stat->dl_state == DL_DATA)
    {
      /* Check for more available data from the socket */
      bytesread = 0;
      
      bytesread = dl_recvdata (dlconn, (void *) &dlconn->stat->databuf[dlconn->stat->recptr],
			       BUFSIZE - dlconn->stat->recptr, dlconn->sladdr);
      
      if (bytesread < 0 && ! dlconn->terminate)            /* read() failed */
	{
	  dlconn->link = dl_disconnect (dlconn);
	  dlconn->stat->netdly_trig = -1;
	}
      else if (bytesread > 0)	/* Data is here, process it */
	{
	  dlconn->stat->recptr += bytesread;
	  
	  /* Reset the timeout and keepalive timers */
	  dlconn->stat->netto_trig     = -1;
	  dlconn->stat->keepalive_trig = -1;
	}
    }
  
  /* Update timing variables */
  current_time = dl_dtime ();

  /* Network timeout timing logic */
  if (dlconn->netto)
    {
      if (dlconn->stat->netto_trig == -1)	/* reset timer */
	{
	  dlconn->stat->netto_time = current_time;
	  dlconn->stat->netto_trig = 0;
	}
      else if (dlconn->stat->netto_trig == 0 &&
	       (current_time - dlconn->stat->netto_time) > dlconn->netto)
	{
	  dlconn->stat->netto_trig = 1;
	}
    }
  
  /* Keepalive/heartbeat interval timing logic */
  if (dlconn->keepalive)
    {
      if (dlconn->stat->keepalive_trig == -1)	/* reset timer */
	{
	  dlconn->stat->keepalive_time = current_time;
	  dlconn->stat->keepalive_trig = 0;
	}
      else if (dlconn->stat->keepalive_trig == 0 &&
	       (current_time - dlconn->stat->keepalive_time) > dlconn->keepalive)
	{
	  dlconn->stat->keepalive_trig = 1;
	}
    }
  
  /* Network delay timing logic */
  if (dlconn->netdly)
    {
      if (dlconn->stat->netdly_trig == -1)	/* reset timer */
	{
	  dlconn->stat->netdly_time = current_time;
	  dlconn->stat->netdly_trig = 1;
	}
      else if (dlconn->stat->netdly_trig == 1 &&
	       (current_time - dlconn->stat->netdly_time) > dlconn->netdly)
	{
	  dlconn->stat->netdly_trig = 0;
	}
    }
  
  /* Non-blocking and no data was returned */
  return SLNOPACKET;
  
}  /* End of dl_collect_nb() */


/***************************************************************************
 * dl_request_info:
 *
 * Add an INFO request to the Datalink Connection Description.
 *
 * Returns 0 if successful and -1 if error.
 ***************************************************************************/
int
dl_request_info (DLCP *dlconn, const char * infostr)
{
  if ( dlconn->info != NULL )
    {
      dl_log_r (dlconn, 2, 0, "Cannot make '%.15s' INFO request, one is already pending\n",
		infostr);
      return -1;
    }
  else
    {
      dlconn->info = infostr;
      return 0;
    }
}  /* End of dl_request_info() */


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
  int64_t pvalue;
  int64_t size = 0;
  int rv = 0;
  
  if ( ! dlconn || ! buffer )
    return -1;
  
  /* Make sure buffer if terminated */
  buffer[buflen] = '\0';
  
  /* Parse reply header */
  if ( sscanf (buffer, "%10s %lld %lld", status, &pvalue, &size) != 3 )
    {
      dl_log_r (dlconn, 2, 0, "Unable to parse reply header: '%s'\n", buffer);
      return -1;
    }
  
  /* Store reply value if requested */
  if ( value )
    *value = pvalue;
  
  /* Check that reply message will fit into buffer */
  if ( size > buflen )
    {
      dl_log_r (dlconn, 2, 0, "Reply message too large (%d) for buffer (%d)\n",
		size, buflen);
      return -1;      
    }
  
  /* Receive reply message if included */
  if ( size > 0 )
    {
      /* Receive reply message */
      if ( dl_recvdata (dlconn, buffer, size) != size )
	{
	  dl_log_r (dlconn, 2, 0, "Problem receiving reply message\n");
	  return -1;
	}
      
      if ( size < buflen )
	buffer[size] = '\0';
      else
	buffer[buflen-1] = '\0';
    }
  /* Make sure buffer is terminated */
  else
    {
      buffer[0] = '\0';
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
      dl_log_r (dlconn, 2, 0, "Unrecognized reply string %.5s\n", buffer);
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
  dl_log_r (dlconn, 1, 1, "Terminating connection\n");

  dlconn->terminate = 1;
}  /* End of dl_terminate() */
