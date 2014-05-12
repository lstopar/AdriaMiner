#
# GLib makefile, builds all
#

include ../Makefile.config

# library headers include
#QMINER = ../../QMiner
#LIBV8 = $(QMINER)/lib/v8/include
LIBUV = ../../libuv/include

GLIB = ../../glib
GLIB_BASE = $(GLIB)/base/
GLIB_NET = $(GLIB)/net/
GLIB_MINE = $(GLIB)/mine/
GLIB_MISC = $(GLIB)/misc/
GLIB_THREAD = $(GLIB)/concurrent/
#BOOST = ../../boost/

INCLUDE = -I. -I$(GLIB_BASE) -I$(GLIB_NET) -I$(GLIB_MINE) -I$(GLIB_MISC) -I$(GLIB_THREAD) -I$(LIBUV)

# main object files
MAINOBJS = main.o adria_server.o

all: $(MAINOBJS)

%.o: %.cpp
	$(CC) -c $(CXXFLAGS) $< $(INCLUDE) $(LDFLAGS) $(LIBS)
	
clean:
	rm -f *.o *.gch *.a

	
