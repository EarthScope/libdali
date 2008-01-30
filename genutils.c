/***************************************************************************
 * genutils.c
 *
 * General utility functions.
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * Version: 2008.030
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libdali.h"


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
