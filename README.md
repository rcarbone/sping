This directory holds a collection of event-based ping programs,
written by scratch one by one and each adding functionalities
to its precessor as they are identified.

sping.c - The simplest ping

    It works like a very rudimental ping(8) but all the data
    values hard coded in the program.

    It does not support any command line options to modify in any way
    the ICMP request to be sent on the wire.

    Obviuosly the name of the host to ping must be passed as argument.

    Limits:
     o global variables used
     o just a single host could be pinged
     o sending interval between sending each packet fixed to 500 milliseconds

    Ported to libevent2 on Wed Feb 11 08:18:14 CET 2015
