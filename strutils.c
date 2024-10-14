/***********************************************************************/ /**
 * @file strutils.c
 *
 * Generic routines for string manipulation
 *
 * This file is part of the DataLink Library.
 *
 * Copyright (c) 2023 Chad Trabant, EarthScope Data Services
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ***************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "libdali.h"

/***********************************************************************/ /**
 * @brief Copy a string while removing space charaters
 *
 * Copy @a length characters from @a source to @a dest while removing
 * all spaces.  The result is left justified and always null
 * terminated.  The source string must have at least @a length
 * characters and the destination string must have enough room needed
 * for the non-space characters within @a length and the null
 * terminator.
 *
 * @param dest Destination string
 * @param source String to copy
 * @param length Copy up to a maximum of this many characters to @a dest
 *
 * @return The number of characters (not including the null
 * terminator) in the destination string.
 ***************************************************************************/
int
dl_strncpclean (char *dest, const char *source, int length)
{
  int sidx, didx;

  for (sidx = 0, didx = 0; sidx < length; sidx++)
  {
    if (*(source + sidx) != ' ')
    {
      *(dest + didx) = *(source + sidx);
      didx++;
    }
  }

  *(dest + didx) = '\0';

  return didx;
} /* End of dl_strncpclean() */

/***********************************************************************/ /**
 * @brief Concatinate one string to another growing the destination as needed
 *
 * Concatinate one string to another with a delimiter in-between
 * growing the destination string as needed up to a maximum length.
 *
 * @param string Destination string to be added to
 * @param add String to add to @a string
 * @param delim Optional delimiter between added strings (cannot be NULL, but can be an empty string)
 * @param maxlen Maximum number of bytes to grow @a string
 *
 * @return 0 on success, -1 on memory allocation error and -2 when
 * string would grow beyond maximum length.
 ***************************************************************************/
int
dl_addtostring (char **string, char *add, char *delim, int maxlen)
{
  size_t length;
  char *ptr;

  if (!string || !add)
    return -1;

  /* If string is empty, allocate space and copy the addition */
  if (!*string)
  {
    length = strlen (add) + 1;

    if (length > maxlen)
      return -2;

    if ((*string = (char *)malloc (length)) == NULL)
      return -1;

    strcpy (*string, add);
  }
  /* Otherwise tack on the addition with a delimiter */
  else
  {
    length = strlen (*string) + strlen (delim) + strlen (add) + 1;

    if (length > maxlen)
      return -2;

    if ((ptr = (char *)realloc (*string, length)) == NULL)
      return -1;

    *string = ptr;

    strcat (*string, delim);
    strcat (*string, add);
  }

  return 0;
} /* End of dl_addtostring() */
