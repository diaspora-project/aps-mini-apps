# Compiler
CC = clang++

# Directories
ROOTDIR = ../../
# DISP Framework src dir
DISPDIR = /Users/bicer/Projects/DISP/src/core/
# Bin directory
BINDIR = ${ROOTDIR}/bin/

# Flags
CFLAGS = -std=c++11 -DAFFINITY_OFF -g -O3 -Wall -Wextra 

# Common data structures and utilities classes
COMMONINC = ${ROOTDIR}/src/common

HDF5LIB = /Users/bicer/Projects/hdf5-1.8.13/hdf5/lib
HDF5INC = /Users/bicer/Projects/hdf5-1.8.13/hdf5/include 

MPILIB = /usr/local/lib/
MPIINC = /usr/local/include/

LIBS = -lm -lhdf5 -lz -lmpi 
LIBDIRS = -L$(HDF5LIB) -L$(MPILIB)
INCLUDES = -I$(DISPDIR) -I${COMMONINC} -I$(HDF5INC) -I$(MPIINC)

# Executable/reconstruction objects
SIRT_OBJS = sirt_main.o

# Executables
PROGS = sirt

.PHONY: default clean

all: $(PROGS)

# SIRT
sirt: $(SIRT_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SIRT_OBJS) $(TIAR_OBJS) $(INCLUDES) $(LIBDIRS) $(LIBS)
sirt_main.o: sirt_main.cc
	$(CC) $(CFLAGS) -c sirt_main.cc $(INCLUDES)
# sirt.o: sirt.cc
# 	$(CC) $(CFLAGS) -c sirt.cc $(INCLUDES)

install:
	mv $(PROGS) $(BINDIR)

clean:
	rm -f $(PROGS) *.o *.a *~ *.lst *.tmp .pure *.bak *.log
