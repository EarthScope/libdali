
/***************************************************************************
 * network.c
 *
 * Network communication routines for DataLink
 *
 * Written by Chad Trabant, 
 *   IRIS Data Management Center
 *
 * Version: 2008.012
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libdali.h"

/* Functions only used in this source file */
int dl_sayhello (DLCP * dlconn);
int dl_negotiate_uni (DLCP * dlconn);
int dl_negotiate_multi (DLCP * dlconn);


/***************************************************************************
 * dl_configlink:
 *
 * Configure/negotiate data stream(s) with the remote Datalink
 * server.  Negotiation will be either uni or multi-station
 * depending on the value of 'multistation' in the DLCP
 * struct.
 *
 * Returns -1 on errors, otherwise returns the link descriptor.
 ***************************************************************************/
int
dl_configlink (DLCP * dlconn)
{
  int ret = -1;

  if (dlconn->multistation)
    {
      if (dl_checkversion (dlconn, 2.5) >= 0)
	{
	  ret = dl_negotiate_multi (dlconn);
	}
      else
	{
	  dl_log_r (dlconn, 2, 0,
		    "[%s] detected  Datalink version (%.3f) does not support multi-station protocol\n",
		    dlconn->sladdr, dlconn->protocol_ver);
	  ret = -1;
	}
    }
  else
    ret = dl_negotiate_uni (dlconn);

  return ret;
}  /* End of dl_configlink() */


/***************************************************************************
 * dl_negotiate_uni:
 *
 * Negotiate a Datalink connection in uni-station mode and issue
 * the DATA command.  This is compatible with Datalink Protocol
 * version 2 or greater.
 *
 * If 'selectors' != 0 then the string is parsed on space and each
 * selector is sent.
 *
 * If 'seqnum' != -1 and the DLCP 'resume' flag is true then data is
 * requested starting at seqnum.
 *
 * Returns -1 on errors, otherwise returns the link descriptor.
 ***************************************************************************/
int
dl_negotiate_uni (DLCP * dlconn)
{
  int sellen = 0;
  int bytesread = 0;
  int acceptsel = 0;		/* Count of accepted selectors */
  char *selptr;
  char *extreply = 0;
  char *term1, *term2;
  char sendstr[100];		/* A buffer for command strings */
  char readbuf[100];		/* A buffer for responses */
  SLstream *curstream;
  
  /* Point to the stream chain */
  curstream = dlconn->streams;

  selptr = curstream->selectors;
  
  /* Send the selector(s) and check the response(s) */
  if (curstream->selectors != 0)
    {
      while (1)
	{
	  selptr += sellen;
	  selptr += strspn (selptr, " ");
	  sellen = strcspn (selptr, " ");

	  if (sellen == 0)
	    break;		/* end of while loop */

	  else if (sellen > SELSIZE)
	    {
	      dl_log_r (dlconn, 2, 0, "[%s] invalid selector: %.*s\n", dlconn->sladdr,
			sellen, selptr);
	      selptr += sellen;
	    }
	  else
	    {
	      
	      /* Build SELECT command, send it and receive response */
	      sprintf (sendstr, "SELECT %.*s\r", sellen, selptr);
	      dl_log_r (dlconn, 1, 2, "[%s] sending: SELECT %.*s\n", dlconn->sladdr,
			sellen, selptr);
	      bytesread = dl_senddata (dlconn, (void *) sendstr,
				       strlen (sendstr), dlconn->sladdr,
				       readbuf, sizeof (readbuf));
	      if (bytesread < 0)
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
	      
	      /* Check response to SELECT */
	      if (!strncmp (readbuf, "OK\r", 3) && bytesread >= 4)
		{
		  dl_log_r (dlconn, 1, 2, "[%s] selector %.*s is OK %s%s%s\n", dlconn->sladdr,
			    sellen, selptr, (extreply)?"{":"", (extreply)?extreply:"", (extreply)?"}":"");
		  acceptsel++;
		}
	      else if (!strncmp (readbuf, "ERROR\r", 6) && bytesread >= 7)
		{
		  dl_log_r (dlconn, 1, 2, "[%s] selector %.*s not accepted %s%s%s\n", dlconn->sladdr,
			    sellen, selptr, (extreply)?"{":"", (extreply)?extreply:"", (extreply)?"}":"");
		}
	      else
		{
		  dl_log_r (dlconn, 2, 0,
			    "[%s] invalid response to SELECT command: %.*s\n",
			    dlconn->sladdr, bytesread, readbuf);
		  return -1;
		}
	    }
	}
      
      /* Fail if none of the given selectors were accepted */
      if (!acceptsel)
	{
	  dl_log_r (dlconn, 2, 0, "[%s] no data stream selector(s) accepted\n",
		    dlconn->sladdr);
	  return -1;
	}
      else
	{
	  dl_log_r (dlconn, 1, 2, "[%s] %d selector(s) accepted\n",
		    dlconn->sladdr, acceptsel);
	}
    }				/* End of selector processing */

  /* Issue the DATA, FETCH or TIME action commands.  A specified start (and
     optionally, stop time) takes precedence over the resumption from any
     previous sequence number. */
  if (dlconn->begin_time != NULL)
    {
      if (dl_checkversion (dlconn, (float)2.92) >= 0)
	{
	  if (dlconn->end_time == NULL)
	    {
	      sprintf (sendstr, "TIME %.25s\r", dlconn->begin_time);
	    }
	  else
	    {
	      sprintf (sendstr, "TIME %.25s %.25s\r", dlconn->begin_time,
		       dlconn->end_time);
	    }
	  dl_log_r (dlconn, 1, 1, "[%s] requesting specified time window\n",
		    dlconn->sladdr);
	}
      else
	{
	  dl_log_r (dlconn, 2, 0,
		    "[%s] detected Datalink version (%.3f) does not support TIME windows\n",
		    dlconn->sladdr, dlconn->protocol_ver);
	}
    }
  else if (curstream->seqnum != -1 && dlconn->resume )
    {
      char cmd[10];

      if ( dlconn->dialup )
	{
	  sprintf (cmd, "FETCH");
	}
      else
	{
	  sprintf (cmd, "DATA");
	}

      /* Append the last packet time if the feature is enabled and server is >= 2.93 */
      if (dlconn->lastpkttime &&
	  dl_checkversion (dlconn, (float)2.93) >= 0 &&
	  strlen (curstream->timestamp))
	{
	  /* Increment sequence number by 1 */
	  sprintf (sendstr, "%s %06X %.25s\r", cmd,
		   (curstream->seqnum + 1) & 0xffffff, curstream->timestamp);

	  dl_log_r (dlconn, 1, 1, "[%s] resuming data from %06X (Dec %d) at %.25s\n",
		    dlconn->sladdr, (curstream->seqnum + 1) & 0xffffff,
		    (curstream->seqnum + 1), curstream->timestamp);
	}
      else
	{
	  /* Increment sequence number by 1 */
	  sprintf (sendstr, "%s %06X\r", cmd, (curstream->seqnum + 1) & 0xffffff);

	  dl_log_r (dlconn, 1, 1, "[%s] resuming data from %06X (Dec %d)\n",
		    dlconn->sladdr, (curstream->seqnum + 1) & 0xffffff,
		    (curstream->seqnum + 1));
	}
    }
  else
    {
      if ( dlconn->dialup )
	{
	  sprintf (sendstr, "FETCH\r");
	}
      else
	{
	  sprintf (sendstr, "DATA\r");
	}

      dl_log_r (dlconn, 1, 1, "[%s] requesting next available data\n", dlconn->sladdr);
    }

  if (dl_senddata (dlconn, (void *) sendstr, strlen (sendstr),
		   dlconn->sladdr, (void *) NULL, 0) < 0)
    {
      dl_log_r (dlconn, 2, 0, "[%s] error sending DATA/FETCH/TIME request\n", dlconn->sladdr);
      return -1;
    }

  return dlconn->link;
}  /* End of dl_negotiate_uni() */


/***************************************************************************
 * dl_negotiate_multi:
 *
 * Negotiate a Datalink connection using multi-station mode and 
 * issue the END action command.  This is compatible with Datalink
 * Protocol version 3, multi-station mode.
 *
 * If 'curstream->selectors' != 0 then the string is parsed on space
 * and each selector is sent.
 *
 * If 'curstream->seqnum' != -1 and the DLCP 'resume' flag is true
 * then data is requested starting at seqnum.
 *
 * Returns -1 on errors, otherwise returns the link descriptor.
 ***************************************************************************/
int
dl_negotiate_multi (DLCP * dlconn)
{
  int sellen = 0;
  int bytesread = 0;
  int acceptsta = 0;		/* Count of accepted stations */
  int acceptsel = 0;		/* Count of accepted selectors */
  char *selptr;
  char *term1, *term2;
  char *extreply = 0;
  char sendstr[100];		/* A buffer for command strings */
  char readbuf[100];		/* A buffer for responses */
  char slring[12];		/* Keep track of the ring name */
  SLstream *curstream;

  /* Point to the stream chain */
  curstream = dlconn->streams;

  /* Loop through the stream chain */
  while (curstream != NULL)
    {

      /* A ring identifier */
      snprintf (slring, sizeof (slring), "%s_%s",
		curstream->net, curstream->sta);

      /* Send the STATION command */
      sprintf (sendstr, "STATION %s %s\r", curstream->sta, curstream->net);
      dl_log_r (dlconn, 1, 2, "[%s] sending: STATION %s %s\n",
		slring, curstream->sta, curstream->net);
      bytesread = dl_senddata (dlconn, (void *) sendstr,
			       strlen (sendstr), slring, readbuf,
			       sizeof (readbuf));
      if (bytesread < 0)
	{
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
      
      /* Check the response */
      if (!strncmp (readbuf, "OK\r", 3) && bytesread >= 4)
	{
	  dl_log_r (dlconn, 1, 2, "[%s] station is OK %s%s%s\n", slring,
		    (extreply)?"{":"", (extreply)?extreply:"", (extreply)?"}":"");
	  acceptsta++;
	}
      else if (!strncmp (readbuf, "ERROR\r", 6) && bytesread >= 7)
	{
	  dl_log_r (dlconn, 2, 0, "[%s] station not accepted %s%s%s\n", slring,
		    (extreply)?"{":"", (extreply)?extreply:"", (extreply)?"}":"");
	  /* Increment the loop control and skip to the next stream */
	  curstream = curstream->next;
	  continue;
	}
      else
	{
	  dl_log_r (dlconn, 2, 0, "[%s] invalid response to STATION command: %.*s\n",
		    slring, bytesread, readbuf);
	  return -1;
	}

      selptr = curstream->selectors;
      sellen = 0;

      /* Send the selector(s) and check the response(s) */
      if (curstream->selectors != 0)
	{
	  while (1)
	    {
	      selptr += sellen;
	      selptr += strspn (selptr, " ");
	      sellen = strcspn (selptr, " ");

	      if (sellen == 0)
		break;		/* end of while loop */

	      else if (sellen > SELSIZE)
		{
		  dl_log_r (dlconn, 2, 0, "[%s] invalid selector: %.*s\n",
			    slring, sellen, selptr);
		  selptr += sellen;
		}
	      else
		{

		  /* Build SELECT command, send it and receive response */
		  sprintf (sendstr, "SELECT %.*s\r", sellen, selptr);
		  dl_log_r (dlconn, 1, 2, "[%s] sending: SELECT %.*s\n", slring, sellen,
			    selptr);
		  bytesread = dl_senddata (dlconn, (void *) sendstr,
					   strlen (sendstr), slring,
 					   readbuf, sizeof (readbuf));
		  if (bytesread < 0)
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
		  
		  /* Check response to SELECT */
		  if (!strncmp (readbuf, "OK\r", 3) && bytesread >= 4)
		    {
		      dl_log_r (dlconn, 1, 2, "[%s] selector %.*s is OK %s%s%s\n", slring,
				sellen, selptr, (extreply)?"{":"", (extreply)?extreply:"", (extreply)?"}":"");
		      acceptsel++;
		    }
		  else if (!strncmp (readbuf, "ERROR\r", 6) && bytesread >= 7)
		    {
		      dl_log_r (dlconn, 2, 0, "[%s] selector %.*s not accepted %s%s%s\n", slring,
				sellen, selptr, (extreply)?"{":"", (extreply)?extreply:"", (extreply)?"}":"");
		    }
		  else
		    {
		      dl_log_r (dlconn, 2, 0,
				"[%s] invalid response to SELECT command: %.*s\n",
				slring, bytesread, readbuf);
		      return -1;
		    }
		}
	    }

	  /* Fail if none of the given selectors were accepted */
	  if (!acceptsel)
	    {
	      dl_log_r (dlconn, 2, 0, "[%s] no data stream selector(s) accepted",
			slring);
	      return -1;
	    }
	  else
	    {
	      dl_log_r (dlconn, 1, 2, "[%s] %d selector(s) accepted\n", slring,
			acceptsel);
	    }

	  acceptsel = 0;	/* Reset the accepted selector count */

	}			/* End of selector processing */

      /* Issue the DATA, FETCH or TIME action commands.  A specified start (and
	 optionally, stop time) takes precedence over the resumption from any
	 previous sequence number. */
      if (dlconn->begin_time != NULL)
	{
	  if (dl_checkversion (dlconn, (float)2.92) >= 0)
	    {
	      if (dlconn->end_time == NULL)
		{
		  sprintf (sendstr, "TIME %.25s\r", dlconn->begin_time);
		}
	      else
		{
		  sprintf (sendstr, "TIME %.25s %.25s\r", dlconn->begin_time,
			   dlconn->end_time);
		}
	      dl_log_r (dlconn, 1, 1, "[%s] requesting specified time window\n",
			slring);
	    }
	  else
	    {
	      dl_log_r (dlconn, 2, 0,
			"[%s] detected Datalink version (%.3f) does not support TIME windows\n",
			slring, dlconn->protocol_ver);
	    }
	}
      else if (curstream->seqnum != -1 && dlconn->resume )
	{
	  char cmd[10];
	  
	  if ( dlconn->dialup )
	    {
	      sprintf (cmd, "FETCH");
	    }
	  else
	    {
	      sprintf (cmd, "DATA");
	    }
	  
	  /* Append the last packet time if the feature is enabled and server is >= 2.93 */
	  if (dlconn->lastpkttime &&
	      dl_checkversion (dlconn, (float)2.93) >= 0 &&
	      strlen (curstream->timestamp))
	    {
	      /* Increment sequence number by 1 */
	      sprintf (sendstr, "%s %06X %.25s\r", cmd,
		       (curstream->seqnum + 1) & 0xffffff, curstream->timestamp);

	      dl_log_r (dlconn, 1, 1, "[%s] resuming data from %06X (Dec %d) at %.25s\n",
			dlconn->sladdr, (curstream->seqnum + 1) & 0xffffff,
			(curstream->seqnum + 1), curstream->timestamp);
	    }	  
	  else
	    { /* Increment sequence number by 1 */
	      sprintf (sendstr, "%s %06X\r", cmd,
		       (curstream->seqnum + 1) & 0xffffff);

	      dl_log_r (dlconn, 1, 1, "[%s] resuming data from %06X (Dec %d)\n", slring,
			(curstream->seqnum + 1) & 0xffffff,
			(curstream->seqnum + 1));
	    }
	}
      else
	{
	  if ( dlconn->dialup )
	    {
	      sprintf (sendstr, "FETCH\r");
	    }
	  else
	    {
	      sprintf (sendstr, "DATA\r");
	    }
	  
	  dl_log_r (dlconn, 1, 1, "[%s] requesting next available data\n", slring);
	}

      /* Send the TIME/DATA/FETCH command and receive response */
      bytesread = dl_senddata (dlconn, (void *) sendstr,
			       strlen (sendstr), slring,
			       readbuf, sizeof (readbuf));
      if (bytesread < 0)
	{
	  dl_log_r (dlconn, 2, 0, "[%s] error with DATA/FETCH/TIME request\n", slring);
	  return -1;
	}
      
      /* Search for 2nd "\r" indicating extended reply message present */
      extreply = 0;
      if ( (term1 = memchr (readbuf, '\r', bytesread)) )
	{
	  if ( (term2 = memchr (term1+1, '\r', bytesread-(readbuf-term1)-1)) )
	    {
	      fprintf (stderr, "term2: '%s'\n", term2);

	      *term2 = '\0';
	      extreply = term1+1;
	    }
	}
      
      /* Check response to DATA/FETCH/TIME request */
      if (!strncmp (readbuf, "OK\r", 3) && bytesread >= 4)
	{
	  dl_log_r (dlconn, 1, 2, "[%s] DATA/FETCH/TIME command is OK %s%s%s\n", slring,
		    (extreply)?"{":"", (extreply)?extreply:"", (extreply)?"}":"");
	}
      else if (!strncmp (readbuf, "ERROR\r", 6) && bytesread >= 7)
	{
	  dl_log_r (dlconn, 2, 0, "[%s] DATA/FETCH/TIME command is not accepted %s%s%s\n", slring,
		    (extreply)?"{":"", (extreply)?extreply:"", (extreply)?"}":"");
	}
      else
	{
	  dl_log_r (dlconn, 2, 0, "[%s] invalid response to DATA/FETCH/TIME command: %.*s\n",
		    slring, bytesread, readbuf);
	  return -1;
	}
      
      /* Point to the next stream */
      curstream = curstream->next;

    }  /* End of stream and selector config (end of stream chain). */
  
  /* Fail if no stations were accepted */
  if (!acceptsta)
    {
      dl_log_r (dlconn, 2, 0, "[%s] no station(s) accepted\n", dlconn->sladdr);
      return -1;
    }
  else
    {
      dl_log_r (dlconn, 1, 1, "[%s] %d station(s) accepted\n",
	      dlconn->sladdr, acceptsta);
    }

  /* Issue END action command */
  sprintf (sendstr, "END\r");
  dl_log_r (dlconn, 1, 2, "[%s] sending: END\n", dlconn->sladdr);
  if (dl_senddata (dlconn, (void *) sendstr, strlen (sendstr),
		   dlconn->sladdr, (void *) NULL, 0) < 0)
    {
      dl_log_r (dlconn, 2, 0, "[%s] error sending END command\n", dlconn->sladdr);
      return -1;
    }

  return dlconn->link;
}  /* End of dl_negotiate_multi() */


/***************************************************************************
 * dl_send_info:
 *
 * Send a request for the specified INFO level.  The verbosity level
 * can be specified, allowing control of when the request should be
 * logged.
 *
 * Returns -1 on errors, otherwise the socket descriptor.
 ***************************************************************************/
int
dl_send_info (DLCP * dlconn, const char * info_level, int verbose)
{
  char sendstr[40];		/* A buffer for command strings */

  if (dl_checkversion (dlconn, (float)2.92) >= 0)
    {
      sprintf (sendstr, "INFO %.15s\r", info_level);

      dl_log_r (dlconn, 1, verbose, "[%s] requesting INFO level %s\n",
		dlconn->sladdr, info_level);

      if (dl_senddata (dlconn, (void *) sendstr, strlen (sendstr),
		       dlconn->sladdr, (void *) NULL, 0) < 0)
	{
	  dl_log_r (dlconn, 2, 0, "[%s] error sending INFO request\n", dlconn->sladdr);
	  return -1;
	}
    }
  else
    {
      dl_log_r (dlconn, 2, 0,
		"[%s] detected Datalink version (%.3f) does not support INFO requests\n",
		dlconn->sladdr, dlconn->protocol_ver);   
      return -1;
    }

  return dlconn->link;
}  /* End of dl_send_info() */


/***************************************************************************
 * dl_connect:
 *
 * Open a network socket connection to a Datalink server and set
 * 'dlconn->link' to the new descriptor.  Expects 'dlconn->sladdr' to
 * be in 'host:port' format.  Either the host, port or both are
 * optional, if the host is not specified 'localhost' is assumed, if
 * the port is not specified '18000' is assumed, if neither is
 * specified (only a colon) then 'localhost' and port '18000' are
 * assumed.
 *
 * If sayhello is true the HELLO command will be issued after
 * successfully connecting, this sets the server version in the DLCP
 * and generally verifies that the remove process is a Datalink
 * server and is probably always desired.
 *
 * If a permanent error is detected (invalid port specified) the
 * dlconn->terminate flag will be set so the dl_collect() family of
 * routines will not continue trying to connect.
 *
 * Returns -1 on errors otherwise the socket descriptor created.
 ***************************************************************************/
int
dl_connect (DLCP * dlconn, int sayhello)
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

  if ( slp_sockstartup() ) {
    dl_log_r (dlconn, 2, 0, "could not initialize network sockets\n");
    return -1;
  }
  
  /* Check server address string and use defaults if needed:
   * If only ':' is specified neither host nor port specified
   * If no ':' is included no port was specified
   * If ':' is the first character no host was specified
   */
  if ( ! strcmp (dlconn->sladdr, ":") )
    {
      strcpy (nodename, "localhost");
      strcpy (nodeport, "18000");
    }
  else if ((ptr = strchr (dlconn->sladdr, ':')) == NULL)
    {
      strncpy (nodename, dlconn->sladdr, sizeof(nodename));
      strcpy (nodeport, "18000");
    }
  else
    {
      if ( ptr == dlconn->sladdr )
	{
	  strcpy (nodename, "localhost");
	}
      else
	{
	  strncpy (nodename, dlconn->sladdr, (ptr - dlconn->sladdr));
	  nodename[(ptr - dlconn->sladdr)] = '\0';
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
  
  if ( slp_getaddrinfo (nodename, nodeport, &addr, &addrlen) )
    {
      dl_log_r (dlconn, 2, 0, "cannot resolve hostname %s\n", nodename );
      return -1;
    }

  if ((sock = socket (PF_INET, SOCK_STREAM, 0)) < 0)
    {
      dl_log_r (dlconn, 2, 0, "[%s] socket(): %s\n", dlconn->sladdr, slp_strerror ());
      slp_sockclose (sock);
      return -1;
    }

  /* Set non-blocking IO */
  if ( slp_socknoblock(sock) )
    {
      dl_log_r (dlconn, 2, 0, "Error setting socket to non-blocking\n");
    }

  if ( (slp_sockconnect (sock, (struct sockaddr *) &addr, addrlen)) )
    {
      dl_log_r (dlconn, 2, 0, "[%s] connect(): %s\n", dlconn->sladdr, slp_strerror ());
      slp_sockclose (sock);
      return -1;
    }
  
  /* Wait up to 10 seconds for the socket to be connected */
  if ((sockstat = dl_checksock (sock, 10, 0)) <= 0)
    {
      if (sockstat < 0)
	{			/* select() returned error */
	  dl_log_r (dlconn, 2, 1, "[%s] socket connect error\n", dlconn->sladdr);
	}
      else
	{			/* socket time-out */
	  dl_log_r (dlconn, 2, 1, "[%s] socket connect time-out (10s)\n",
		    dlconn->sladdr);
	}

      slp_sockclose (sock);
      return -1;
    }
  else if ( ! dlconn->terminate )
    {				/* socket connected */
      dl_log_r (dlconn, 1, 1, "[%s] network socket opened\n", dlconn->sladdr);
      
      /* Set the SO_KEEPALIVE socket option, although not really useful */
      if (setsockopt
	  (sock, SOL_SOCKET, SO_KEEPALIVE, (char *) &on, sizeof (on)) < 0)
	dl_log_r (dlconn, 1, 1, "[%s] cannot set SO_KEEPALIVE socket option\n",
		  dlconn->sladdr);
      
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
 * Returns -1, historically used to set the old descriptor
 ***************************************************************************/
int
dl_disconnect (DLCP * dlconn)
{
  if (dlconn->link != -1)
    {
      slp_sockclose (dlconn->link);
      dlconn->link = -1;
      
      dl_log_r (dlconn, 1, 1, "[%s] network socket closed\n", dlconn->sladdr);
    }
  
  return -1;
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
  dl_log_r (dlconn, 1, 2, "[%s] sending: %s\n", dlconn->sladdr, sendstr);
  dl_senddata (dlconn, (void *) sendstr, strlen (sendstr), dlconn->sladdr,
	       NULL, 0);
  
  /* Recv the two lines of response: server ID and site installation ID */
  if ( dl_recvresp (dlconn, (void *) servstr, (size_t) sizeof (servstr),
		    sendstr, dlconn->sladdr) < 0 )
    {
      return -1;
    }
  
  if ( dl_recvresp (dlconn, (void *) sitestr, (size_t) sizeof (sitestr),
		    sendstr, dlconn->sladdr) < 0 )
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
  dl_log_r (dlconn, 1, 1, "[%s] connected to: %s\n", dlconn->sladdr, servstr);
  if ( capptr )
    dl_log_r (dlconn, 1, 1, "[%s] capabilities: %s\n", dlconn->sladdr, capptr);
  dl_log_r (dlconn, 1, 1, "[%s] organization: %s\n", dlconn->sladdr, sitestr);
  
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
                dlconn->sladdr);
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
		      dlconn->sladdr, tptr);
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
      dl_log_r (dlconn, 1, 2, "[%s] sending: %s\n", dlconn->sladdr, sendstr);
      bytesread = dl_senddata (dlconn, (void *) sendstr, strlen (sendstr), dlconn->sladdr,
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
	  dl_log_r (dlconn, 1, 2, "[%s] capabilities OK %s%s%s\n", dlconn->sladdr,
		    (extreply)?"{":"", (extreply)?extreply:"", (extreply)?"}":"");
	}
      else if (!strncmp (readbuf, "ERROR\r", 6) && bytesread >= 7)
	{
	  dl_log_r (dlconn, 1, 2, "[%s] CAPABILITIES not accepted %s%s%s\n", dlconn->sladdr,
		    (extreply)?"{":"", (extreply)?extreply:"", (extreply)?"}":"");	  
	  return -1;
	}
      else
	{
	  dl_log_r (dlconn, 2, 0,
		    "[%s] invalid response to CAPABILITIES command: %.*s\n",
		    dlconn->sladdr, bytesread, readbuf);
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
  dl_log_r (dlconn, 1, 2, "[%s] sending: HELLO\n", dlconn->sladdr);
  dl_senddata (dlconn, (void *) sendstr, strlen (sendstr), dlconn->sladdr,
	       NULL, 0);
  
  /* Recv the two lines of response */
  if ( dl_recvresp (dlconn, (void *) servstr, (size_t) sizeof (servstr), 
		    sendstr, dlconn->sladdr) < 0 )
    {
      return -1;
    }
  
  if ( dl_recvresp (dlconn, (void *) sitestr, (size_t) sizeof (sitestr),
		    sendstr, dlconn->sladdr) < 0 )
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
  
  dlconn->link = dl_disconnect (dlconn);
  
  return 0;
}  /* End of dl_ping() */


/***************************************************************************
 * dl_checksock:
 *
 * Check a socket for write ability using select() and read ability
 * using recv(... MSG_PEEK).  Time-out values are also passed (seconds
 * and microseconds) for the select() call.
 *
 * Returns:
 *  1 = success
 *  0 = if time-out expires
 * -1 = errors
 ***************************************************************************/
int
dl_checksock (int sock, int tosec, int tousec)
{
  int sret;
  int ret = -1;			/* default is failure */
  char testbuf[1];
  fd_set checkset;
  struct timeval to;

  FD_ZERO (&checkset);
  FD_SET ((unsigned int)sock, &checkset);

  to.tv_sec = tosec;
  to.tv_usec = tousec;

  /* Check write ability with select() */
  if ((sret = select (sock + 1, NULL, &checkset, NULL, &to)) > 0)
    ret = 1;
  else if (sret == 0)
    ret = 0;			/* time-out expired */

  /* Check read ability with recv() */
  if (ret && (recv (sock, testbuf, sizeof (char), MSG_PEEK)) <= 0)
    {
      if (! slp_noblockcheck())
	ret = 1;		/* no data for non-blocking IO */
      else
	ret = -1;
    }

  return ret;
}  /* End of dl_checksock() */


/***************************************************************************
 * dl_senddata:
 *
 * send() 'buflen' bytes from 'buffer' to 'dlconn->link'.  'ident' is
 * a string to include in error messages for identification, usually
 * the address of the remote server.  If 'resp' is not NULL then read
 * up to 'resplen' bytes into 'resp' after sending 'buffer'.  This is
 * only designed for small pieces of data, specifically the server
 * responses to commands terminated by '\r\n'.
 *
 * Returns -1 on error, and size (in bytes) of the response
 * received (0 if 'resp' == NULL).
 ***************************************************************************/
int
dl_senddata (DLCP * dlconn, void *buffer, size_t buflen,
	     const char *ident, void *resp, int resplen)
{
  
  int bytesread = 0;		/* bytes read into resp */
  
  if ( send (dlconn->link, buffer, buflen, 0) < 0 )
    {
      dl_log_r (dlconn, 2, 0, "[%s] error sending '%.*s'\n", ident,
		strcspn ((char *) buffer, "\r\n"), (char *) buffer);
      return -1;
    }
  
  /* If requested collect the response */
  if ( resp != NULL )
    {
      /* Clear response buffer */
      memset (resp, 0, resplen);
      
      bytesread = dl_recvresp (dlconn, resp, resplen, buffer, ident);
    }
  
  return bytesread;
}  /* End of dl_senddata() */


/***************************************************************************
 * dl_recvdata:
 *
 * recv() 'maxbytes' data from 'dlconn->link' into a specified
 * 'buffer'.  'ident' is a string to be included in error messages for
 * identification, usually the address of the remote server.
 *
 * Returns -1 on error/EOF, 0 for no available data and the number
 * of bytes read on success.
 ***************************************************************************/
int
dl_recvdata (DLCP * dlconn, void *buffer, size_t maxbytes,
	     const char *ident)
{
  int bytesread = 0;
  
  if ( buffer == NULL )
    {
      return -1;
    }
  
  bytesread = recv (dlconn->link, buffer, maxbytes, 0);
  
  if ( bytesread == 0 )		/* should indicate TCP FIN or EOF */
    {
      dl_log_r (dlconn, 1, 1, "[%s] recv():%d TCP FIN or EOF received\n",
		ident, bytesread);
      return -1;
    }
  else if ( bytesread < 0 )
    {
      if ( slp_noblockcheck() )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] recv():%d %s\n", ident, bytesread,
		    slp_strerror ());
	  return -1;
	}

      /* no data available for NONBLOCKing IO */
      return 0;
    }

  return bytesread;
}  /* End of dl_recvdata() */


/***************************************************************************
 * dl_recvresp:
 *
 * To receive a response to a command recv() one byte at a time until
 * '\r\n' or up to 'maxbytes' is read from 'dlconn->link' into a
 * specified 'buffer'.  The function will wait up to 30 seconds for a
 * response to be recv'd.  'command' is a string to be included in
 * error messages indicating which command the response is
 * for. 'ident' is a string to be included in error messages for
 * identification, usually the address of the remote server.
 *
 * It should not be assumed that the populated buffer contains a
 * terminated string.
 *
 * Returns -1 on error/EOF and the number of bytes read on success.
 ***************************************************************************/
int
dl_recvresp (DLCP * dlconn, void *buffer, size_t maxbytes,
	     const char *command, const char *ident)
{
  
  int bytesread = 0;		/* total bytes read */
  int recvret   = 0;            /* return from dl_recvdata */
  int ackcnt    = 0;		/* counter for the read loop */
  int ackpoll   = 50000;	/* poll at 0.05 seconds for reading */
  
  if ( buffer == NULL )
    {
      return -1;
    }
  
  /* Clear the receiving buffer */
  memset (buffer, 0, maxbytes);
  
  /* Recv a byte at a time and wait up to 30 seconds for a response */
  while ( bytesread < maxbytes )
    {
      recvret = dl_recvdata (dlconn, (char *)buffer + bytesread, 1, ident);
      
      /* Trap door for termination */
      if ( dlconn->terminate )
	{
	  return -1;
	}
      
      if ( recvret > 0 )
	{
	  bytesread += recvret;
	}
      else if ( recvret < 0 )
	{
	  dl_log_r (dlconn, 2, 0, "[%s] bad response to '%.*s'\n",
		    ident, strcspn ((char *) command, "\r\n"),
		    (char *) command);
	  return -1;
	}
      
      /* Trap door if '\r\n' is recv'd */
      if ( bytesread >= 2 &&
	   *(char *)((char *)buffer + bytesread - 2) == '\r' &&
	   *(char *)((char *)buffer + bytesread - 1) == '\n' )
	{
	  return bytesread;
	}
      
      /* Trap door if 30 seconds has elapsed, (ackpoll x 600) */
      if ( ackcnt > 600 )
        {
	  dl_log_r (dlconn, 2, 0, "[%s] timeout waiting for response to '%.*s'\n",
		    ident, strcspn ((char *) command, "\r\n"),
		    (char *) command);
	  return -1;
	}
      
      /* Delay if no data received */
      if ( recvret == 0 )
	{
	  slp_usleep (ackpoll);
	  ackcnt++;
	}
    }
  
  return bytesread;
}  /* End of dl_recvresp() */
