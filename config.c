/***************************************************************************
 * config.c:
 *
 * Routines to assist with the configuration of a DataLink connection
 * description.
 *
 * Written by Chad Trabant, ORFEUS/EC-Project MEREDIAN
 *
 * modified: 2008.031
 ***************************************************************************/

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "libdali.h"


/***************************************************************************
 * dl_read_streamlist:
 *
 * Read a list of streams and selectors from a file and add them to the 
 * stream chain for configuring a multi-station connection.
 *
 * If 'defselect' is not NULL or 0 it will be used as the default selectors
 * for entries will no specific selectors indicated.
 *
 * The file is expected to be repeating lines of the form:
 *   <NET> <STA> [selectors]
 * For example:
 * --------
 * # Comment lines begin with a '#' or '*'
 * GE ISP  BH?.D
 * NL HGN
 * MN AQU  BH?  HH?
 * -------- 
 *
 * Returns the number of streams configured or -1 on error.
 ***************************************************************************/
int
dl_read_streamlist (DLCP *dlconn, const char *streamfile,
		    const char *defselect)
{
  int streamfd;
  char net[3];
  char sta[6];
  char selectors[100];
  char line[100];
  int fields;
  int count;
  int stacount;
  int addret;

  net[0] = '\0';
  sta[0] = '\0';
  selectors[0] = '\0';

  /* Open the stream list file */
  if ( (streamfd = dlp_openfile (streamfile, 'r')) < 0 )
    {
      if ( errno == ENOENT )
	{
	  dl_log_r (dlconn, 2, 0, "could not find stream list file: %s\n", streamfile);
	  return -1;
	}
      else
	{
	  dl_log_r (dlconn, 2, 0, "opening stream list file, %s\n", strerror (errno));
	  return -1;
	}
    }

  dl_log_r (dlconn, 1, 1, "Reading stream list from %s\n", streamfile);

  count = 1;
  stacount = 0;

  while ( (dl_readline (streamfd, line, sizeof(line))) >= 0 )
    {
      fields = sscanf (line, "%2s %5s %99[a-zA-Z0-9?. ]\n",
		       net, sta, selectors);
      
      /* Ignore blank or comment lines */
      if ( fields < 0 || net[0] == '#' || net[0] == '*' )
	continue;

      if ( fields < 2 )
	{
	  dl_log_r (dlconn, 2, 0, "cannot parse line %d of stream list\n", count);
	}
      
      /* Add this stream to the stream chain */
      if ( fields == 3 )
	{
	  dl_addstream (dlconn, net, sta, selectors, -1, NULL);
	  stacount++;
	}
      else
	{
	  addret = dl_addstream (dlconn, net, sta, defselect, -1, NULL);
	  stacount++;
	}
      
	count++;
    }

  if ( stacount == 0 )
    {
      dl_log_r (dlconn, 2, 0, "no streams defined in %s\n", streamfile);
    }
  else if ( stacount > 0 )
    {
      dl_log_r (dlconn, 1, 2, "Read %d streams from %s\n", stacount, streamfile);
    }

  if ( close (streamfd) )
    {
      dl_log_r (dlconn, 2, 0, "closing stream list file, %s\n", strerror (errno));
      return -1;
    }
  
  return count;
}  /* End of dl_read_streamlist() */
