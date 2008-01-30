/***************************************************************************
 * statefile.c:
 *
 * Routines to save and recover DataLink state information to/from a file.
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * modified: 2008.030
 ***************************************************************************/

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "libdali.h"


/***************************************************************************
 * dl_savestate:
 *
 * Save the all the current the sequence numbers and time stamps into the
 * given state file.
 *
 * Returns:
 * -1 : error
 *  0 : completed successfully
 ***************************************************************************/
int
dl_savestate (DLCP * dlconn, const char *statefile)
{
  char line[200];
  int linelen;
  int statefd;
  
  curstream = dlconn->streams;
  
  /* Open the state file */
  if ( (statefd = dlp_openfile (statefile, 'w')) < 0 )
    {
      dl_log_r (dlconn, 2, 0, "cannot open state file for writing\n");
      return -1;
    }
  
  dl_log_r (dlconn, 1, 2, "saving connection state to state file\n");
  
  /* Write state information */
  linelen = snprintf (line, sizeof(line), "%s %lld %lld\n",
		      dlconn->addr, dlconn->stat->pktid, dlconn->stat->pkttime);
  
  if ( write (statefd, line, linelen) != linelen )
    {
      dl_log_r (dlconn, 2, 0, "cannot write to state file, %s\n", strerror (errno));
      return -1;
    }
  
  /* Close the state file */
  if ( close (statefd) )
    {
      dl_log_r (dlconn, 2, 0, "cannot close state file, %s\n", strerror (errno));
      return -1;
    }
  
  return 0;
} /* End of dl_savestate() */

CHAD, recover needs fixing for DataLink, ie no streams.


/***************************************************************************
 * dl_recoverstate:
 *
 * Recover the state file and put the sequence numbers and time stamps into
 * the pre-existing stream chain entries.
 *
 * Returns:
 * -1 : error
 *  0 : completed successfully
 *  1 : file could not be opened (probably not found)
 ***************************************************************************/
int
dl_recoverstate (DLCP * dlconn, const char *statefile)
{
  SLstream *curstream;
  int statefd;
  char net[3];
  char sta[6];
  char timestamp[20];
  char line[100];
  int seqnum;
  int fields;
  int count;

  net[0] = '\0';
  sta[0] = '\0';
  timestamp[0] = '\0';

  /* Open the state file */
  if ( (statefd = dlp_openfile (statefile, 'r')) < 0 )
    {
      if ( errno == ENOENT )
	{
	  dl_log_r (dlconn, 1, 0, "could not find state file: %s\n", statefile);
	  return 1;
	}
      else
	{
	  dl_log_r (dlconn, 2, 0, "could not open state file, %s\n", strerror (errno));
	  return -1;
	}
    }

  dl_log_r (dlconn, 1, 1, "recovering connection state from state file\n");

  count = 1;

  while ( (dl_readline (statefd, line, sizeof(line))) >= 0 )
    {
      fields = sscanf (line, "%2s %5s %d %19[0-9,]\n",
		       net, sta, &seqnum, timestamp);

      if ( fields < 0 )
        continue;
      
      if ( fields < 3 )
	{
	  dl_log_r (dlconn, 2, 0, "could not parse line %d of state file\n", count);
	}
      
      /* Search for a matching NET and STA in the stream chain */
      curstream = dlconn->streams;
      while (curstream != NULL)
	{
	  if (!strcmp (net, curstream->net) &&
	      !strcmp (sta, curstream->sta))
	    {
	      curstream->seqnum = seqnum;

	      if ( fields == 4 )
		strncpy (curstream->timestamp, timestamp, 20);

	      break;
	    }
	  curstream = curstream->next;
	}

      count++;
    }

  if ( close (statefd) )
    {
      dl_log_r (dlconn, 2, 0, "could not close state file, %s\n", strerror (errno));
      return -1;
    }

  return 0;
} /* End of dl_recoverstate() */
