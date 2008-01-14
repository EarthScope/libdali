/***************************************************************************
 * genutils.c
 *
 * General utility functions.
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * Version: 2008.013
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
