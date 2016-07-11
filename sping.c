/*
 * sping.c - 'sping' is the simplest libevent-based ICMP ping program
 *
 * Copyright (C) 2009-2016 Rocco Carbone <rocco@tecsiel.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


/* Operating System header file(s) */
#include <stdio.h>
#include <errno.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>

/* Libevent header file(s) */
#include "event2/event.h"
struct event;

/* Packets definitions */

/* max IP packet size is 65536 while fixed IP header size is 20;
 * the traditional ping program transmits 56 bytes of data, so the
 * default data size is calculated as to be like the original
 */
#define IPHDR           20
#define MIN_DATA_SIZE   sizeof (data_t)
#define DFL_DATA_SIZE   (MIN_DATA_SIZE + 44)         /* calculated as so to be like traditional ping */
#define MAX_DATA_SIZE   (IP_MAXPACKET - IPHDR - ICMP_MINLEN)

#define DFL_PING_INTERVAL (500 * 1000)


/* Data added to the ICMP header for the purpose to relate request/response */
typedef struct
{
  uint32_t magic;                 /* magic number         */
  struct timeval ts;              /* time packet was sent */
} data_t;


/* Global variables */
static uint16_t whoami;           /* process pid                               */
static int fd;	                  /* raw socket used to ping hosts             */
static char * hostname;           /* who to ping (as given by user)            */
static struct sockaddr_in saddr;  /* internet address of who to ping           */
static uint32_t pktsize;          /* packet size (ICMP plus User Data) to send */
static struct timeval interval;   /* interval between sending ping packets     */
static struct event * timer;      /* libevent timer to */


static void push_cb (int unused, const short event, void * arg);


/* Return the full qualified hostname */
static char * fqname (struct in_addr in)
{
  struct hostent * h = gethostbyaddr ((char *) & in, sizeof (struct in_addr), AF_INET);

  return ! h || ! h -> h_name ? inet_ntoa (in) : h -> h_name;
}


/*
 * render time into a string with three digits of precision
 * input is in tens of microseconds
 */
static char * fmttime (int t)
{
  static char buf [10];

  /* <= 0.99 ms */
  if (t < 100)
    sprintf (buf, "0.%0d", t);

  /* 1.00 - 9.99 ms */
  else if (t < 1000)
    sprintf (buf, "%d.%02d", t / 100, t % 100);

  /* 10.0 - 99.9 ms */
  else if (t < 10000)
    sprintf (buf, "%d.%d", t / 100, (t % 100) / 10);

  /* >= 100 ms */
  else
    sprintf (buf, "%d", t / 100);

  return buf;
}


/*
 * Checksum routine for Internet Protocol family headers (C Version).
 * From ping examples in W. Richard Stevens "Unix Network Programming" book
 */
static int mkcksum (u_short * p, int n)
{
  u_short answer;
  long sum = 0;
  u_short odd_byte = 0;

  while (n > 1)
    {
      sum += * p ++;
      n -= 2;
    }

  /* mop up an odd byte, if necessary */
  if (n == 1)
    {
      * (u_char *) (& odd_byte) = * (u_char *) p;
      sum += odd_byte;
    }

  sum = (sum >> 16) + (sum & 0xffff);	/* add high 16 to low 16 */
  sum += (sum >> 16);			/* add carry */
  answer = ~sum;			/* ones-complement, truncate */

  return answer;
}


/*
 * Format an ICMP_ECHO REQUEST packet
 *  - the IP packet will be added on by the kernel
 *  - the ID field is the Unix process ID
 *  - the sequence number is an ascending integer
 *
 *  The first 8 bytes of the data portion are used
 *  The first 8 bytes of the data portion are used
 *  to hold a Unix "timeval" struct in VAX byte-order,
 *  to compute the round-trip time.
 */
static void fmticmp (u_char * buffer, int size, u_int8_t seq)
{
  struct icmp * icmp = (struct icmp *) buffer;
  data_t * data = (data_t *) (buffer + ICMP_MINLEN);

  struct timeval now;

  /* The ICMP header (no checksum here until user data has been filled in) */
  icmp -> icmp_type = ICMP_ECHO;    /* type of message */
  icmp -> icmp_code = 0;            /* type sub code */
  icmp -> icmp_id   = whoami;       /* unique application identifier */
  icmp -> icmp_seq  = htons (seq);  /* message identifier */

  /* User data */
  gettimeofday (& now, NULL);
  data -> magic = 0xd4c3d2a1;       /* a magic */
  data -> ts    = now;

  /* Last, compute ICMP checksum */
  icmp -> icmp_cksum = mkcksum ((u_short *) icmp, size);  /* ones complement checksum of struct */
}


/* Attempt to transmit a ping message to a host */
static void push_cb (int unused, const short event, void * arg)
{
  static u_int8_t seq = 1;
  static int once = 0;

  u_char packet [MAX_DATA_SIZE] = "";
  int nsent;

  /* Format the Echo reply message to send */
  fmticmp (packet, pktsize, seq ++);

  /* Transmit the request over the network */
  nsent = sendto (fd, packet, pktsize, MSG_DONTWAIT, (struct sockaddr *) & saddr, sizeof (struct sockaddr_in));
  if (nsent != pktsize)
    printf ("%s error while sending ping [%s]\n", hostname, strerror (errno));
  else if (! once)
    {
      printf ("PING %s (%s) %d(%d) bytes of data.\n",
	      fqname (saddr . sin_addr), inet_ntoa (saddr . sin_addr),
	      pktsize - ICMP_MINLEN, pktsize + IPHDR);
      once = 1;
    }
}


/* Read packet from the wire and attempt to decode and relate ICMP echo reply request/response
 *
 * To be cool the packet received must be:
 *  o of enough size (> IPHDR + ICMP_MINLEN)
 *  o of type ICMP_ECHOREPLY
 *  o the one we are looking for (same identifier of all the packets the program is able to send)
 */
static void data_cb (int unused, const short event, void * arg)
{
  int nrecv;
  u_char packet [MAX_DATA_SIZE];
  struct sockaddr_in remote;              /* responding internet address */
  socklen_t slen = sizeof (struct sockaddr);

  /* Pointer to relevant portions of the packet (IP, ICMP and user data) */
  struct ip * ip = (struct ip *) packet;
  struct icmphdr * icmp;
  data_t * data = (data_t *) (packet + IPHDR + ICMP_MINLEN);
  int hlen = 0;

  struct timeval now;
  struct timeval elapsed;             /* response time */

  /* Time the packet has been received */
  gettimeofday (& now, NULL);

  /* Receive data from the network */
  nrecv = recvfrom (fd, packet, sizeof (packet), MSG_DONTWAIT, (struct sockaddr *) & remote, & slen);
  if (nrecv < 0)
    return;

  /* Calculate the IP header length */
  hlen = ip -> ip_hl * 4;

  /* Check the IP header */
  if (nrecv < hlen + ICMP_MINLEN || ip -> ip_hl < 5)
    {
      printf ("received packet too short for ICMP (%d bytes from %s)\n",
	      nrecv, inet_ntoa (remote . sin_addr));
      return;
    }

  /* The ICMP portion */
  icmp = (struct icmphdr *) (packet + hlen);

  /* Drop unexpected packets */
  if (icmp -> type != ICMP_ECHOREPLY)
    return;

  if (icmp -> un . echo . id != whoami)
    {
      printf ("received unexpected packet - id %u != %u (%d bytes from %s)\n",
	      icmp -> un . echo . id, whoami,
	      nrecv, inet_ntoa (remote . sin_addr));
      return;
    }

  /* Compute time difference */
  evutil_timersub (& now, & data -> ts, & elapsed);

  printf ("%ld bytes from %s (%s): icmp_seq=%d ttl=%d time=%s ms\n",
	  (long) nrecv - (sizeof (struct icmp) - sizeof (struct icmphdr)),
	  fqname (remote . sin_addr),
	  inet_ntoa (remote . sin_addr),
	  ntohs (icmp -> un . echo . sequence),
	  ip -> ip_ttl, fmttime (elapsed . tv_usec / 10));

  /* Start the ping timer at given time interval */
  evtimer_add (timer, & interval);
}


/* Obtain from the OS all that is required to perform the task of pinging hosts */
static int initialize (char * progname, char * me)
{
  struct protoent * proto;
  int fd;
  struct sockaddr_in sa;
  struct in_addr src;

  /* Check if the ICMP protocol is available on this system */
  if (! (proto = getprotobyname ("icmp")))
    {
      printf ("%s: unsupported protocol icmp\n", progname);
      return -1;
    }

  /* Create an endpoint for communication using raw socket for ICMP calls */
  if ((fd = socket (AF_INET, SOCK_RAW, proto -> p_proto)) == -1)
    {
      printf ("%s: can't create raw socket (errno %d - %s)\n", progname, errno, strerror (errno));
      return -1;
    }

  if (me)
    {
      memset (& sa, 0, sizeof (sa));
      sa . sin_family = AF_INET;
      sa . sin_addr = src;

      if (! inet_pton (AF_INET, me, & src) ||
	  bind (fd, (struct sockaddr *) & sa, sizeof (sa)) == -1)
	{
	  printf ("%s: cannot bind source address '%s' (errno %d - %s)\n", progname, me, errno, strerror (errno));
	  close (fd);
	  return -1;
	}
    }

  return fd;
}


/* Like ping, but with network performances in mind */
int main (int argc, char * argv [])
{
  struct event_base * base;

  /* Notice the program name */
  char * progname = strrchr (argv [0], '/');
  progname = ! progname ? * argv : progname + 1;

  /* Initialize global variables */
  whoami = getpid () & 0xffff;
  pktsize = DFL_DATA_SIZE + ICMP_MINLEN;

  interval . tv_sec = 0;
  interval . tv_usec = DFL_PING_INTERVAL;   /* interval between sending ping packets (in millisec) */

  /* Initialize the libevent */
  base = event_base_new ();

  /* Check for at least one mandatory parameter */
  argv ++;
  if (! argv || ! * argv)
    printf ("%s: missing argument\n", progname);
  else
    {
      /* Handle only the first host name supplied on the command line */

      struct event * read_evt; /* Used to detect read events */
      struct hostent * host;

      /* Initialize the application */
      if ((fd = initialize (progname, NULL)) == -1)
	return 1;

      /* Define the callback to send ping packets */
      timer = evtimer_new (base, push_cb, NULL);

      /* Add the raw file descriptor to the list of those monitored for read events */
      read_evt = event_new (base, fd, EV_READ | EV_PERSIST, data_cb, NULL);
      event_add (read_evt, NULL);

      /* Setup remote address */
      memset (& saddr, '\0', sizeof (struct sockaddr_in));
      saddr . sin_family = AF_INET;

      host = gethostbyname (* argv);
      if (host)
	memcpy (& saddr . sin_addr, host -> h_addr_list [0], host -> h_length);
      else
	saddr . sin_addr . s_addr = inet_addr (* argv);
      if (saddr . sin_addr . s_addr == -1)
	{
	  printf ("%s: unknown host %s\n", progname, * argv);
	  return 1;
	}

      /* Save the hostname */
      hostname = * argv;

      /* Start the timer to transmit an ICMP request over the network */
      evtimer_add (timer, & interval);

      /* Event dispatching loop */
      event_base_dispatch (base);
    }

  /* Terminate the libevent library */
  event_base_free (base);

  return 0;
}
