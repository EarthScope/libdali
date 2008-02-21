/***************************************************************************
 * daliclient.c
 *
 * An example DataLink client demonstrating the use of libdali.
 *
 * Connects to a DataLink server, configures a connection and collects
 * data.  Detailed information about the data received can be printed.
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * modified 2008.051
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libdali.h>

#ifndef WIN32
  #include <signal.h>

  static void term_handler (int sig);
#else
  #define strcasecmp _stricmp
#endif

#define PACKAGE "daliclient"
#define VERSION LIBDALI_VERSION

static short int verbose   = 0;
static short int ppackets  = 0;
static char *statefile     = 0;	    /* State file for saving/restoring state */
static char *matchpattern  = 0;	    /* Source ID matching expression */
static char *rejectpattern = 0;	    /* Source ID rejecting expression */

static DLCP *dlconn;	            /* Connection parameters */

static void packet_handler (char *msrecord, int packet_type,
			    int seqnum, int packet_size);
static int parameter_proc (int argcount, char **argvec);
static void usage (void);


int
main (int argc, char **argv)
{
  DLPacket *dlpack;
  int64_t value;
  
#ifndef WIN32
  /* Signal handling, use POSIX calls with standardized semantics */
  struct sigaction sa;

  sigemptyset (&sa.sa_mask);
  sa.sa_flags   = SA_RESTART;
  
  sa.sa_handler = term_handler;
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGQUIT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);
  
  sa.sa_handler = SIG_IGN;
  sigaction (SIGHUP, &sa, NULL);
  sigaction (SIGPIPE, &sa, NULL);
#endif

  /* Allocate and initialize a new connection description */
  dlconn = dl_newdlcp();
  
  /* Process given parameters (command line and parameter file) */
  if ( parameter_proc (argc, argv) < 0 )
    {
      fprintf (stderr, "Parameter processing failed\n\n");
      fprintf (stderr, "Try '-h' for detailed help\n");
      return -1;
    }
  
  /* Connect to server */
  if ( dl_connect (dlcp) < 0 )
    {
      fprintf (stderr, "Error connecting to server\n");
      return -1;
    }
  
  /* Reposition connection */
  if ( dlconn->pktid > 0 )
    {
      if ( (rv = dl_position (dlconn, dlconn->pktid, dlconn->pkttime)) < 0 )
	return -1;
      else
	dl_log (1, 1, "Reposition connection to packet ID %lld\n", rv);
    }
  
  /* Send match pattern if supplied */
  if ( matchpattern )
    {
      rv = dl_match (dlcp, matchpattern);
      
      if ( rv < 0 )
	return -1;
      else
	dl_log (1, 1, "Matching %d current streams\n", rv);
    }
  
  /* Send reject pattern if supplied */
  if ( rejectpattern )
    {
      rv = dl_match (dlcp, rejectpattern);
      
      if ( rv < 0 )
	return -1;
      else
	dl_log (1, 1, "Rejecting %d current streams\n", rv);
    }
  
  // CHAD, need to rework this main collect loop and the packet_handler()
  
  /* Loop with the connection manager */
  while ( dl_collect (dlconn, &slpack) )
    {
      ptype  = dl_packettype (slpack);
      seqnum = dl_sequence (slpack);
      
      packet_handler ((char *) &slpack->msrecord, ptype, seqnum, SLRECSIZE);
    }
  
  /* Make sure everything is shut down and save the state file */
  if ( dlconn->link != -1 )
    dl_disconnect (dlconn);
  
  /* Save the state file */
  if ( statefile )
    dl_savestate (dlconn, statefile);
  
  return 0;
}  /* End of main() */


/***************************************************************************
 * packet_handler():
 * Process a received packet based on packet type.
 ***************************************************************************/
static void
packet_handler (char *msrecord, int packet_type, int seqnum, int packet_size)
{
  static SLMSrecord * msr = NULL;

  double dtime;			/* Epoch time */
  double secfrac;		/* Fractional part of epoch time */
  time_t itime;			/* Integer part of epoch time */
  char timestamp[20];
  struct tm *timep;

  /* The following is dependent on the packet type values in libslink.h */
  char *type[]  = { "Data", "Detection", "Calibration", "Timing",
		    "Message", "General", "Request", "Info",
                    "Info (terminated)", "KeepAlive" };

  /* Build a current local time string */
  dtime   = dl_dtime ();
  secfrac = (double) ((double)dtime - (int)dtime);
  itime   = (time_t) dtime;
  timep   = localtime (&itime);
  snprintf (timestamp, 20, "%04d.%03d.%02d:%02d:%02d.%01.0f",
	    timep->tm_year + 1900, timep->tm_yday + 1, timep->tm_hour,
	    timep->tm_min, timep->tm_sec, secfrac);

  /* Process waveform data */
  if ( packet_type == SLDATA )
    {
      dl_log (0, 1, "%s, seq %d, Received %s blockette:\n",
	      timestamp, seqnum, type[packet_type]);

      dl_msr_parse (dlconn->log, msrecord, &msr, 1, 0);

      if ( verbose || ppackets )
	dl_msr_print (dlconn->log, msr, ppackets);
    }
  else if ( packet_type == SLKEEP )
    {
      dl_log (0, 2, "Keep alive packet received\n");
    }
  else
    {
      dl_log (0, 1, "%s, seq %d, Received %s blockette\n",
	      timestamp, seqnum, type[packet_type]);
    }
}  /* End of packet_handler() */


/***************************************************************************
 * parameter_proc:
 *
 * Process the command line parameters.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static int
parameter_proc (int argcount, char **argvec)
{
  int optind;
  int error = 0;
  
  char *pattern = 0;
  
  if (argcount <= 1)
    error++;
  
  /* Process all command line arguments */
  for (optind = 1; optind < argcount; optind++)
    {
      if (strcmp (argvec[optind], "-V") == 0)
	{
	  fprintf(stderr, "%s version: %s\n", PACKAGE, VERSION);
	  exit (0);
	}
      else if (strcmp (argvec[optind], "-h") == 0)
	{
	  usage();
	  exit (0);
	}
      else if (strncmp (argvec[optind], "-v", 2) == 0)
	{
	  verbose += strspn (&argvec[optind][1], "v");
	}
      else if (strcmp (argvec[optind], "-p") == 0)
	{
	  ppackets = 1;
	}
      else if (strcmp (argvec[optind], "-m") == 0)
	{
	  matchpattern = argvec[++optind];
	}
      else if (strcmp (argvec[optind], "-r") == 0)
	{
	  rejectpattern = argvec[++optind];
	}
      else if (strcmp (argvec[optind], "-S") == 0)
	{
	  statefile = argvec[++optind];
	}
      else if (strncmp (argvec[optind], "-", 1 ) == 0)
	{
	  fprintf(stderr, "Unknown option: %s\n", argvec[optind]);
	  exit (1);
	}
      else if ( ! dlconn->addr )
	{
	  dlconn->addr = argvec[optind];
	}
      else
	{
	  fprintf(stderr, "Unknown option: %s\n", argvec[optind]);
	  exit (1);
	}
    }
  
  /* Make sure a server was specified */
  if ( ! dlconn->addr )
    {
      fprintf(stderr, "No DataLink server specified\n\n");
      fprintf(stderr, "Usage: %s [options] [host][:port]\n", PACKAGE);
      fprintf(stderr, "Try '-h' for detailed help\n");
      exit (1);
    }

  /* Initialize the verbosity for the dl_log function */
  dl_loginit (verbose, NULL, NULL, NULL, NULL);
  
  /* Report the program version */
  dl_log (0, 1, "%s version: %s\n", PACKAGE, VERSION);
  
  /* If errors then report the usage message and quit */
  if ( error )
    {
      usage ();
      exit (1);
    }
  
  /* If verbosity is 2 or greater print detailed packet information */
  if ( verbose >= 2 )
    ppackets = 1;
  
  /* Load the match stream list from a file if the argument starts with '@' */
  if ( matchpattern && *matchpattern == '@' )
    {
      char *filename = matchpattern + 1;
      
      if ( ! (matchpattern = dl_read_streamlist (dlconn, filename)) )
	{
	  dl_log (2, 0, "Cannot read matching list file: %s\n", filename);
	  exit (1);
	}
    }

  /* Load the reject stream list from a file if the argument starts with '@' */
  if ( rejectpattern && *rejectpattern == '@' )
    {
      char *filename = rejectpattern + 1;
      
      if ( ! (rejectpattern = dl_read_streamlist (dlconn, filename)) )
	{
	  dl_log (2, 0, "Cannot read rejecting list file: %s\n", filename);
	  exit (1);
	}
    }
  
  /* Recover from the state file and reposition */
  if ( statefile )
    {
      if ( dl_recoverstate (dlconn, statefile) < 0 )
	{
	  fprintf (stderr, "Error reading state file\n");
	  exit (1);
	}
    }
  
  return 0;
}  /* End of parameter_proc() */


/***************************************************************************
 * usage:
 * Print the usage message and exit.
 ***************************************************************************/
static void
usage (void)
{
  fprintf (stderr, "\nUsage: %s [options] [host][:port]\n\n", PACKAGE);
  fprintf (stderr,
	   " ## General program options ##\n"
	   " -V             report program version\n"
	   " -h             show this usage message\n"
	   " -v             be more verbose, multiple flags can be used\n"
	   " -p             print details of data packets\n\n"
	   " -m match       specify stream ID matching pattern\n"
	   " -r reject      specify stream ID rejecting pattern\n"
	   " -S statefile   save/restore stream state information to this file\n"
	   "\n"
	   " [host][:port]  Address of the SeedLink server in host:port format\n"
	   "                  if host is omitted (i.e. ':16000'), localhost is assumed\n"
	   "                  if :port is omitted (i.e. 'localhost'), 16000 is assumed\n\n");
  
}  /* End of usage() */

#ifndef WIN32
/***************************************************************************
 * term_handler:
 * Signal handler routine. 
 ***************************************************************************/
static void
term_handler (int sig)
{
  dl_terminate (dlconn);
}
#endif

