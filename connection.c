/***************************************************************************
 * connection.c
 *
 * Routines for managing a connection with a DataLink server
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * modified: 2008.013
 ***************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "libdali.h"

/* Function(s) only used in this source file */
int update_stream (DLCP * dlconn, DLPacket * dlpack);


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
dl_collect (DLCP * dlconn, DLPacket ** dlpack)
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
dl_collect_nb (DLCP * dlconn, DLPacket ** dlpack) 
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
 * update_stream:
 *
 * Update the appropriate stream chain entries given a Mini-SEED
 * record.
 *
 * Returns 0 if successfully updated and -1 if not found or error.
 ***************************************************************************/
int
update_stream (DLCP * dlconn, DLPacket * dlpack)
{
  SLstream *curstream;
  struct dl_fsdh_s fsdh;
  int seqnum;
  int swapflag = 0;
  int updates = 0;
  char net[3];
  char sta[6];
  
  if ( (seqnum = dl_sequence (dlpack)) == -1 )
    {
      dl_log_r (dlconn, 2, 0, "update_stream(): could not determine sequence number\b");
      return -1;
    }
  
  /* Copy fixed header */
  memcpy (&fsdh, &dlpack->msrecord, sizeof(struct dl_fsdh_s));
  
  /* Check to see if byte swapping is needed (bogus year makes good test) */
  if ((fsdh.start_time.year < 1900) || (fsdh.start_time.year > 2050))
    swapflag = 1;
  
  /* Change byte order? */
  if ( swapflag )
    {
      dl_gswap2 (&fsdh.start_time.year);
      dl_gswap2 (&fsdh.start_time.day);
    }
  
  curstream = dlconn->streams;
  
  /* Generate some "clean" net and sta strings */
  if ( curstream != NULL )
    {
      dl_strncpclean (net, fsdh.network, 2);
      dl_strncpclean (sta, fsdh.station, 5);
    }
  
  /* For uni-station mode */
  if ( curstream != NULL )
    {
      if ( strcmp (curstream->net, UNINETWORK) == 0 &&
	   strcmp (curstream->sta, UNISTATION) == 0 )
	{
	  int month = 0;
	  int mday = 0;
	  
	  dl_doy2md (fsdh.start_time.year,
		     fsdh.start_time.day,
		     &month, &mday);
	  
	  curstream->seqnum = seqnum;
	  
	  snprintf (curstream->timestamp, 20,
		    "%04d,%02d,%02d,%02d,%02d,%02d",
		    fsdh.start_time.year,
		    month,
		    mday,
		    fsdh.start_time.hour,
		    fsdh.start_time.min,
		    fsdh.start_time.sec);
	  
	  return 0;
	}
    }
  
  /* For multi-station mode, search the stream chain and update all matching entries */
  while ( curstream != NULL )
    {
      /* Use glob matching to match wildcarded network and station codes */
      if ( dl_globmatch (net, curstream->net) &&
	   dl_globmatch (sta, curstream->sta) )
        {
	  int month = 0;
	  int mday = 0;
	  
	  dl_doy2md (fsdh.start_time.year,
		     fsdh.start_time.day,
		     &month, &mday);
	  
	  curstream->seqnum = seqnum;
	  
	  snprintf (curstream->timestamp, 20,
		    "%04d,%02d,%02d,%02d,%02d,%02d",
		    fsdh.start_time.year,
		    month,
		    mday,
		    fsdh.start_time.hour,
		    fsdh.start_time.min,
		    fsdh.start_time.sec);
	  
	  updates++;
        }
      
      curstream = curstream->next;
    }
  
  /* If no updates then no match was found */
  if ( updates == 0 )
    dl_log_r (dlconn, 2, 0, "unexpected data received: %.2s %.6s\n", net, sta);
  
  return (updates == 0) ? -1 : 0;
}  /* End of update_stream() */


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
  DLCP * dlconn;

  dlconn = (DLCP *) malloc (sizeof(DLCP));

  if ( dlconn == NULL )
    {
      dl_log_r (NULL, 2, 0, "new_dlconn(): error allocating memory\n");
      return NULL;
    }

  /* Set defaults */
  dlconn->streams        = NULL;
  dlconn->sladdr         = NULL;
  dlconn->begin_time     = NULL;
  dlconn->end_time       = NULL;

  dlconn->resume         = 1;
  dlconn->multistation   = 0;
  dlconn->dialup         = 0;
  dlconn->lastpkttime    = 0;
  dlconn->terminate      = 0;
  
  dlconn->keepalive      = 0;
  dlconn->netto          = 600;
  dlconn->netdly         = 30;
  
  dlconn->link           = -1;
  dlconn->info           = NULL;
  dlconn->protocol_ver   = 0.0;

  /* Allocate the associated persistent state struct */
  dlconn->stat = (SLstat *) malloc (sizeof(SLstat));
  
  if ( dlconn->stat == NULL )
    {
      dl_log_r (NULL, 2, 0, "new_dlconn(): error allocating memory\n");
      free (dlconn);
      return NULL;
    }

  dlconn->stat->recptr         = 0;
  dlconn->stat->sendptr        = 0;
  dlconn->stat->expect_info    = 0;

  dlconn->stat->netto_trig     = -1;
  dlconn->stat->netdly_trig    = 0;
  dlconn->stat->keepalive_trig = -1;

  dlconn->stat->netto_time     = 0.0;
  dlconn->stat->netdly_time    = 0.0;
  dlconn->stat->keepalive_time = 0.0;

  dlconn->stat->dl_state       = DL_DOWN;
  dlconn->stat->query_mode     = NoQuery;

  dlconn->log = NULL;

  return dlconn;
}  /* End of dl_newdlconn() */


/***************************************************************************
 * dl_freedlcp:
 *
 * Free all memory associated with a DLCP struct including the 
 * associated stream chain and persistent connection state.
 *
 ***************************************************************************/
void
dl_freedlcp (DLCP * dlconn)
{
  SLstream *curstream;
  SLstream *nextstream;

  curstream = dlconn->streams;

  /* Traverse the stream chain and free memory */
  while (curstream != NULL)
    {
      nextstream = curstream->next;

      if ( curstream->selectors != NULL )
	free (curstream->selectors);
      free (curstream);

      curstream = nextstream;
    }

  if ( dlconn->sladdr != NULL )
    free (dlconn->sladdr);

  if ( dlconn->begin_time != NULL )
    free (dlconn->begin_time);

  if ( dlconn->end_time != NULL )
    free (dlconn->end_time);

  if ( dlconn->stat != NULL )
    free (dlconn->stat);

  if ( dlconn->log != NULL )
    free (dlconn->log);

  free (dlconn);
}  /* End of dl_freedlcp() */


/***************************************************************************
 * dl_addstream:
 *
 * Add a new stream entry to the stream chain for the given DLCP
 * struct.  No checking is done for duplicate streams.
 *
 *  - selectors should be 0 if there are none to use
 *  - seqnum should be -1 to start at the next data
 *  - timestamp should be 0 if it should not be used
 *
 * Returns 0 if successfully added or -1 on error.
 ***************************************************************************/
int
dl_addstream (DLCP * dlconn, const char * net, const char * sta,
	      const char * selectors, int seqnum,
	      const char * timestamp)
{
  SLstream *curstream;
  SLstream *newstream;
  SLstream *laststream = NULL;
  
  curstream = dlconn->streams;
  
  /* Sanity, check for a uni-station mode entry */
  if ( curstream )
    {
      if ( strcmp (curstream->net, UNINETWORK) == 0 &&
	   strcmp (curstream->sta, UNISTATION) == 0 )
	{
	  dl_log_r (dlconn, 2, 0, "dl_addstream(): uni-station mode already configured!\n");
	  return -1;
	}
    }
  
  /* Search the stream chain */
  while ( curstream != NULL )
    {
      laststream = curstream;
      curstream = curstream->next;
    }
  
  newstream = (SLstream *) malloc (sizeof(SLstream));
  
  if ( newstream == NULL )
    {
      dl_log_r (dlconn, 2, 0, "dl_addstream(): error allocating memory\n");
      return -1;
    }
  
  newstream->net = strdup(net);
  newstream->sta = strdup(sta);

  if ( selectors == 0 || selectors == NULL )
    newstream->selectors = 0;
  else
    newstream->selectors = strdup(selectors);

  newstream->seqnum = seqnum;

  if ( timestamp == 0 || timestamp == NULL )
    newstream->timestamp[0] = '\0';
  else
    strncpy(newstream->timestamp, timestamp, 20);

  newstream->next = NULL;
  
  if ( dlconn->streams == NULL )
    {
      dlconn->streams = newstream;
    }
  else if ( laststream )
    {
      laststream->next = newstream;
    }  

  dlconn->multistation = 1;

  return 0;
}  /* End of dl_addstream() */


/***************************************************************************
 * dl_setuniparams:
 *
 * Set the parameters for a uni-station mode connection for the
 * given DLCP struct.  If the stream entry already exists, overwrite
 * the previous settings.
 * Also sets the multistation flag to 0 (false).
 *
 *  - selectors should be 0 if there are none to use
 *  - seqnum should be -1 to start at the next data
 *  - timestamp should be 0 if it should not be used
 *
 * Returns 0 if successfully added or -1 on error.
 ***************************************************************************/
int
dl_setuniparams (DLCP * dlconn, const char * selectors,
		 int seqnum, const char * timestamp)
{
  SLstream *newstream;

  newstream = dlconn->streams;

  if ( newstream == NULL )
    {
      newstream = (SLstream *) malloc (sizeof(SLstream));

      if ( newstream == NULL )
	{
	  dl_log_r (dlconn, 2, 0, "dl_setuniparams(): error allocating memory\n");
	  return -1;
	}
    }
  else if ( strcmp (newstream->net, UNINETWORK) != 0 ||
	    strcmp (newstream->sta, UNISTATION) != 0)
    {
      dl_log_r (dlconn, 2, 0, "dl_setuniparams(): multi-station mode already configured!\n");
      return -1;
    }

  newstream->net = UNINETWORK;
  newstream->sta = UNISTATION;

  if ( selectors == 0 || selectors == NULL )
    newstream->selectors = 0;
  else
    newstream->selectors = strdup(selectors);

  newstream->seqnum = seqnum;

  if ( timestamp == 0 || timestamp == NULL )
    newstream->timestamp[0] = '\0';
  else
    strncpy(newstream->timestamp, timestamp, 20);

  newstream->next = NULL;
  
  dlconn->streams = newstream;
  
  dlconn->multistation = 0;

  return 0;
}  /* End of dl_setuniparams() */


/***************************************************************************
 * dl_request_info:
 *
 * Add an INFO request to the Datalink Connection Description.
 *
 * Returns 0 if successful and -1 if error.
 ***************************************************************************/
int
dl_request_info (DLCP * dlconn, const char * infostr)
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
 * dl_sequence:
 *
 * Check for 'SL' signature and sequence number.
 *
 * Returns the packet sequence number of the Datalink packet on success,
 *   0 for INFO packets or -1 on error.
 ***************************************************************************/
int
dl_sequence (const DLPacket * dlpack)
{
  int seqnum;
  char seqstr[7], *sqtail;

  if ( strncmp (dlpack->slhead, SIGNATURE, 2) )
    return -1;

  if ( !strncmp (dlpack->slhead, INFOSIGNATURE, 6) )
    return 0;

  strncpy (seqstr, &dlpack->slhead[2], 6);
  seqstr[6] = '\0';
  seqnum = strtoul (seqstr, &sqtail, 16);

  if ( seqnum & ~0xffffff || *sqtail )
    return -1;

  return seqnum;
}  /* End of dl_sequence() */


/***************************************************************************
 * dl_packettype:
 *
 * Check the type of packet.  First check for an INFO packet then check for
 * the first 'important' blockette found in the data record.  If none of
 * the known marker blockettes are found then it is a regular data record.
 *
 * Returns the packet type; defined in libdali.h.
 ***************************************************************************/
int
dl_packettype (const DLPacket * dlpack)
{
  int b2000 = 0;
  struct dl_fsdh_s *fsdh;

  uint16_t  num_samples;
  int16_t   samprate_fact;
  int16_t   begin_blockette;
  uint16_t  blkt_type;
  uint16_t  next_blkt;

  const struct dl_blkt_head_s *p;
  fsdh = (struct dl_fsdh_s *) &dlpack->msrecord;

  /* Check for an INFO packet */
  if ( !strncmp (dlpack->slhead, INFOSIGNATURE, 6) )
    {
      /* Check if it is terminated */
      if  (dlpack->slhead[SLHEADSIZE - 1] != '*')
	return SLINFT;
      else
	return SLINF;
    }  

  num_samples     = (uint16_t) ntohs(fsdh->num_samples);
  samprate_fact   = (int16_t)  ntohs(fsdh->samprate_fact);
  begin_blockette = (int16_t)  ntohs(fsdh->begin_blockette);

  p = (const struct dl_blkt_head_s *) ((const char *) fsdh + 
				       begin_blockette);

  blkt_type       = (uint16_t) ntohs(p->blkt_type);
  next_blkt       = (uint16_t) ntohs(p->next_blkt);

  do
    {
      if (((const char *) p) - ((const char *) fsdh) >
	  MAX_HEADER_SIZE)
	{
	  return SLNUM;
	}

      if (blkt_type >= 200 && blkt_type <= 299)
	return SLDET;
      if (blkt_type >= 300 && blkt_type <= 399)
	return SLCAL;
      if (blkt_type >= 500 && blkt_type <= 599)
	return SLTIM;
      if (blkt_type == 2000)
	b2000 = 1;

      p = (const struct dl_blkt_head_s *) ((const char *) fsdh +
				    next_blkt);
      
      blkt_type       = (uint16_t) ntohs(p->blkt_type);
      next_blkt       = (uint16_t) ntohs(p->next_blkt);
    }
  while ((const struct dl_fsdh_s *) p != fsdh);

  if (samprate_fact == 0)
    {
      if (num_samples != 0)
	return SLMSG;
      if (b2000)
	return SLBLK;
    }

  return SLDATA;
}  /* End of dl_packettype() */


/***************************************************************************
 * dl_terminate:
 *
 * Set the terminate flag in the DLCP.
 ***************************************************************************/
void
dl_terminate (DLCP * dlconn)
{
  dl_log_r (dlconn, 1, 1, "Terminating connection\n");

  dlconn->terminate = 1;
}  /* End of dl_terminate() */
