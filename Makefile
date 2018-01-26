#
# Simple, full asynchronous Libevent-based, ping-like programs
#
# Copyright (C) 2009-2016 Rocco Carbone <rocco@tecsiel.it>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#

# The root directory for public domain software
PUBDIR     = /usr/local/src

# Macro related to the Libevent library (stable version)
EVENTDIR   = ${PUBDIR}/libevent-2.0.22-stable
LIBEVENTSH = ${EVENTDIR}/.libs/libevent.so
LIBEVENTST = ${EVENTDIR}/.libs/libevent.a

# Private binaries
PROGRAMS   = sping

# Source, object and depend files
SRCS       = sping.c
OBJS       = $(patsubst %.c,%.o, ${SRCS})
DEPS       = $(patsubst %.c,%.M, ${SRCS})

# C compiler and flags
INCLUDE    = -I. -I${EVENTDIR}/include
CC         = gcc
CFLAGS     = -g -Wall -fPIC ${INCLUDE}
SHFLAGS    = -shared
AR         = ar cru

# Private libraries
USEDLIBS   = ${LIBEVENTST}

# Operating System libraries
SYSLIBS    = -lrt

# Main targets
all: ${PROGRAMS}

# Binary programs
${PROGRAMS}: ${OBJS} ${LIBEVENTST}
	@echo "=*= making program $@ =*="
	@${CC} $^ ${SYSLIBS} -o $@

clean:
	@rm -f ${PROGRAMS}
	@rm -f ${OBJS}
	@rm -f *~

distclean: clean
	@rm -f ${DEPS}

# How to make an object file
%.o: %.c
	@echo "=*= making object $@ =*="
	@${CC} -c ${CFLAGS} $<

# How to make a depend file
%.M: %.c
	@echo "=*= making dependencies for $< =*="
	@-${CC} -MM -MT $(<:.c=.o) ${CFLAGS} $< -o $@ # 1> /dev/null 2>& 1

-include $(DEPS)
