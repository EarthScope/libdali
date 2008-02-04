/***************************************************************************
 * genutils.c
 *
 * General utility functions.
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * Version: 2008.034
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libdali.h"

/***************************************************************************
 * dl_splitstreamid:
 *
 * Split stream ID into separate components: "NET_STA_LOC_CHAN/TYPE".
 * Memory for each component must already be allocated.  If a specific
 * component is not desired set the appropriate argument to NULL.
 *
 * Returns 0 on success and -1 on error.
 ***************************************************************************/
int
dl_splitstreamid (char *streamid, char *net, char *sta, char *loc,
		  char *chan, char *type)
{
  char *id;
  char *ptr, *top, *next;
  
  if ( ! streamid )
    return -1;
  
  /* Duplicate stream ID */
  if ( ! (id = strdup(streamid)) )
    return -1;
  
  /* First truncate after the type if included */
  if ( (ptr = strrchr (id, '/')) )
    {
      *ptr++ = '\0';
      
      /* Copy the type if requested */
      if ( type )
        strcpy (type, ptr);
    }
  
  /* Network */
  top = id;
  if ( (ptr = strchr (top, '_')) )
    {
      next = ptr + 1;
      *ptr = '\0';
      
      if ( net )
        strcpy (net, top);
      
      top = next;
    }
  /* Station */
  if ( (ptr = strchr (top, '_')) )
    {
      next = ptr + 1;
      *ptr = '\0';
      if ( sta )
        strcpy (sta, top);
      
      top = next;
    }
  /* Location */
  if ( (ptr = strchr (top, '_')) )
    {
      next = ptr + 1;
      *ptr = '\0';
      
      if ( loc )
        strcpy (loc, top);
      
      top = next;
    }
  /* Channel */
  if ( *top && chan )
    {
      strcpy (chan, top);
    }
  
  /* Free duplicated stream ID */
  if ( id )
    free (id);
  
  return 0;
}  /* End of dl_splitstreamid() */


/***************************************************************************
 * dl_bigendianhost:
 *
 * Determine the byte order of the host machine.  Due to the lack of
 * portable defines to determine host byte order this run-time test is
 * provided.  The code below actually tests for little-endianess, the
 * only other alternative is assumed to be big endian.
 * 
 * Returns 0 if the host is little endian, otherwise 1.
 ***************************************************************************/
int
dl_bigendianhost ()
{
  int16_t host = 1;
  return !(*((int8_t *)(&host)));
}  /* End of dl_bigendianhost() */


/***************************************************************************
 * dl_dabs:
 *
 * Determine the absolute value of an input double, actually just test
 * if the input double is positive multiplying by -1.0 if not and
 * return it.
 * 
 * Returns the positive value of input double.
 ***************************************************************************/
double
dl_dabs (double value)
{
  if ( value < 0.0 )
    value *= -1.0;
  return value;
}  /* End of dl_dabs() */


/***************************************************************************
 * dl_readline:
 *
 * Read characters from a stream (specified as a file descriptor)
 * until a newline character '\n' is read and place them into the
 * supplied buffer.  Reading stops when either a newline character is
 * read or buflen-1 characters have been read.  The buffer will always
 * contain a NULL-terminated string.
 *
 * Returns the number of characters read on success and -1 on error.
 ***************************************************************************/
int
dl_readline (int fd, char *buffer, int buflen)
{
  int nread = 0;
  
  if ( ! buffer )
    return -1;
  
  /* Read data from stream until newline character or max characters */
  while ( nread < (buflen-1) )
    {
      /* Read a single character from the stream */
      if ( read (fd, buffer+nread, 1) != 1 )
        {
          return -1;
        }
      
      /* Trap door for newline character */
      if ( buffer[nread] == '\n' )
        {
          break;
        }
      
      nread++;
    }
  
  /* Terminate string in buffer */
  buffer[nread] = '\0';
  
  return nread;
}  /* End of dl_readline() */
