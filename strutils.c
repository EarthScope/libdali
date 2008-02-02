/***************************************************************************
 * strutils.c
 *
 * Routines for string manipulation
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * modified: 2008.032
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "libdali.h"


/***************************************************************************
 * dl_strparse:
 *
 * splits a 'string' on 'delim' and puts each part into a linked list
 * pointed to by 'list' (a pointer to a pointer).  The last entry has
 * it's 'next' set to 0.  All elements are NULL terminated strings.
 * If both 'string' and 'delim' are NULL then the linked list is
 * traversed and the memory used is free'd and the list pointer is
 * set to NULL.
 *
 * Returns the number of elements added to the list, or 0 when freeing
 * the linked list.
 ***************************************************************************/
int
dl_strparse (const char *string, const char *delim, DLstrlist **list)
{
  const char *beg;			/* beginning of element */
  const char *del;			/* delimiter */
  int stop = 0;
  int count = 0;
  int total;

  DLstrlist *curlist = 0;
  DLstrlist *tmplist = 0;

  if (string != NULL && delim != NULL)
    {
      total = strlen (string);
      beg = string;

      while (!stop)
	{

	  /* Find delimiter */
	  del = strstr (beg, delim);

	  /* Delimiter not found or empty */
	  if (del == NULL || strlen (delim) == 0)
	    {
	      del = string + strlen (string);
	      stop = 1;
	    }

	  tmplist = (DLstrlist *) malloc (sizeof (DLstrlist));
	  tmplist->next = 0;

	  tmplist->element = (char *) malloc (del - beg + 1);
	  strncpy (tmplist->element, beg, (del - beg));
	  tmplist->element[(del - beg)] = '\0';

	  /* Add this to the list */
	  if (count++ == 0)
	    {
	      curlist = tmplist;
	      *list = curlist;
	    }
	  else
	    {
	      curlist->next = tmplist;
	      curlist = curlist->next;
	    }

	  /* Update 'beg' */
	  beg = (del + strlen (delim));
	  if ((beg - string) > total)
	    break;
	}

      return count;
    }
  else
    {
      curlist = *list;
      while (curlist != NULL)
	{
	  tmplist = curlist->next;
	  free (curlist->element);
	  free (curlist);
	  curlist = tmplist;
	}
      *list = NULL;

      return 0;
    }
}  /* End of dl_strparse() */


/***************************************************************************
 * dl_strncpclean:
 *
 * Copy 'length' characters from 'source' to 'dest' while removing all
 * spaces.  The result is left justified and always null terminated.
 * The source string must have at least 'length' characters and the
 * destination string must have enough room needed for the non-space
 * characters within 'length' and the null terminator.
 * 
 * Returns the number of characters (not including the null terminator) in
 * the destination string.
 ***************************************************************************/
int
dl_strncpclean (char *dest, const char *source, int length)
{
  int sidx, didx;

  for ( sidx=0, didx=0; sidx < length ; sidx++ )
    {
      if ( *(source+sidx) != ' ' )
	{
	  *(dest+didx) = *(source+sidx);
	  didx++;
	}
    }

  *(dest+didx) = '\0';

  return didx;
}  /* End of dl_strncpclean() */


/***************************************************************************
 * dl_addtostring:
 *
 * Concatinate one string to another with a delimiter in-between
 * growing the target string as needed up to a maximum length.
 * 
 * Return 0 on success, -1 on memory allocation error and -2 when
 * string would grow beyond maximum length.
 ***************************************************************************/
int
dl_addtostring (char **string, char *add, char *delim, int maxlen)
{
  int length;
  char *ptr;
  
  if ( ! string || ! add )
    return -1;
  
  /* If string is empty, allocate space and copy the addition */
  if ( ! *string )
    {
      length = strlen (add) + 1;
      
      if ( length > maxlen )
        return -2;
      
      if ( (*string = (char *) malloc (length)) == NULL )
        return -1;
      
      strcpy (*string, add);
    }
  /* Otherwise tack on the addition with a delimiter */
  else
    {
      length = strlen (*string) + strlen (delim) + strlen(add) + 1;
      
      if ( length > maxlen )
        return -2;
      
      if ( (ptr = (char *) realloc (*string, length)) == NULL )
        return -1;
      
      *string = ptr;
      
      strcat (*string, delim);
      strcat (*string, add);
    }
  
  return 0;
}  /* End of dl_addtostring() */
