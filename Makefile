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
#LIBV8 = ../QMiner/lib/v8/out/x64.release/obj.target/tools/gyp
GLIB = ../glib
#QMINER = ../QMiner
#BOOST = ../boost/stage/lib

# lib includes
#STATIC_LIBS = $(BOOST)/libboost_system.a $(BOOST)/libboost_filesystem.a $(QMINER)/qm.a $(GLIB)/glib.a $(LIBUV)/libuv.a \
	$(LIBV8_STATIC_LIBS)
STATIC_LIBS = $(GLIB)/glib.a $(LIBUV)/libuv.a


QMOBJS = src/adria_server.o
MAINOBJ = src/main.o

all: adria_miner

adria_miner:
	# compile glib
	make -C $(GLIB)
	# compile adria miner
	make -C src
	# create aminer commandline tool
	#$(CC) -o aminer $(QMOBJS) $(MAINOBJ) $(STATIC_LIBS) $(LDFLAGS) $(LIBS)
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
	make -C $(QMINER) clean

clean:
	make -C $(QMINER) clean
	make -C src clean	
	rm -f *.o *.gch *.a qm
	rm -rf ./build/

install: 
	# prepare installation directory
	mkdir /usr/local/qm-$(VERSION)
	# copy build to installation dir
	cp -r ./build/* /usr/local/qm-$(VERSION)
	# create link for qm commandline tool
	ln /usr/local/qm-$(VERSION)/qm /usr/local/bin/qm
	# set QMINER_HOME environment variable
	echo "QMINER_HOME=/usr/local/qm-$(VERSION)/\nexport QMINER_HOME" > qm.sh
	mv ./qm.sh /etc/profile.d/
	
uninstall:
	# delete installation
	rm -rf /usr/local/qm-$(VERSION)
	# delete link
	rm /usr/local/bin/qm
	# delete environment
	rm /etc/profile.d/qm.sh

doc: cleandoc
	rm -rf doc log-doxygen-dev.txt
	sed "s/00000000/$(DOXYGEN_STIME)/" Doxyfile | sed "s/11111111/$(DOXYGEN_SLVER)/" > Doxyfile-tmp
	$(DOXYGEN) Doxyfile-tmp

cleandoc:
	rm -rf doc
	rm -rf Doxyfile-tmp
	rm -rf log-doxygen.txt

installdoc: doc
	scp -r doc blazf@agava:www/qminer-$(DOXYGEN_TIME)
	ssh blazf@agava ln -fsn qminer-$(DOXYGEN_TIME) www/qminer
