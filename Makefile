#
# QMiner makefile
#
# Prerequisites for documentation:
#  - doxygen:  sudo apt-get install doxygen
#  - GraphViz: sudo apt-get install graphviz
#

include ./Makefile.config

# QMiner version
VERSION = 0.5.0

# dependencies
LIBUV = ../libuv
GLIB = ../glib

STATIC_LIBS = $(GLIB)/glib.a $(LIBUV)/libuv.a

QMOBJS = src/utils.o src/analytics.o src/adria_server.o
MAINOBJ = src/main.o

all: adria_miner

adria_miner:
	# compile glib
	make -C $(GLIB)
	# compile adria miner
	make -C src
	# create aminer commandline tool
	$(CC) -o aminer $(QMOBJS) $(MAINOBJ) $(STATIC_LIBS) $(LDFLAGS) $(LIBS) 
	# create qminer static library
	rm -f aminer.a
#	ar -cvq qm.a $(QMOBJS)
	# prepare instalation directory
	mkdir -p build
	# move in qm commandline tool
	mv ./aminer ./build/
	# copy in unicode definiton files
	cp ../glib/bin/UnicodeDef.Bin ./build/
	# copy in javascript libraries
	
cleanall: clean cleandoc
	make -C $(GLIB) clean

clean:
	make -C src clean	
	rm -f *.o *.gch *.a qm
	rm -rf ./build/
