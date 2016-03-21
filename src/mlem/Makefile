# Compiler
CC = clang++

# Directories
ROOTDIR = ../../
# Bin directory
BINDIR = ${ROOTDIR}/bin/

# Flags
CFLAGS = -std=c++11 -O3 -Wall -Wextra
CFLAGS += -g -ffast-math -fPIC -DTIMERON -DPREFETCHON 

# Common data structures and utilities classes
COMMONDIR = ${ROOTDIR}/src/common
# DISP Framework src dir
DISPDIR = ${COMMONDIR}/DISP/core

HDF5LIB = /Users/bicer/Projects/hdf5-1.8.13/hdf5/lib
HDF5INC = /Users/bicer/Projects/hdf5-1.8.13/hdf5/include 

MPILIB = /usr/local/lib/
MPIINC = /usr/local/include/

LIBS = -lm -lhdf5 -lz -lmpi 
LIBDIRS = -L$(HDF5LIB) -L$(MPILIB)
INCLUDES = -I$(DISPDIR) -I${COMMONDIR} -I$(HDF5INC) -I$(MPIINC)

# Executable/reconstruction objects
MLEM_OBJS = mlem.o mlem_main.o
COMMON_OBJS = trace_h5io.o trace_utils.o

# Executables
PROGS = mlem

.PHONY: default clean

all: $(PROGS)

# SIRT
mlem: $(COMMON_OBJS) $(MLEM_OBJS)
	$(CC) $(CFLAGS) -o $@ $(MLEM_OBJS) $(COMMON_OBJS) $(INCLUDES) $(LIBDIRS) $(LIBS)
mlem_main.o: mlem_main.cc
	$(CC) $(CFLAGS) -c mlem_main.cc $(INCLUDES)
mlem.o: mlem.cc
	$(CC) $(CFLAGS) -c mlem.cc $(INCLUDES)

trace_h5io.o: $(COMMONDIR)/trace_h5io.cc $(COMMONDIR)/trace_h5io.h
	$(CC) $(CFLAGS) -c $(COMMONDIR)/trace_h5io.cc $(INCLUDES)

trace_utils.o: $(COMMONDIR)/trace_utils.cc $(COMMONDIR)/trace_utils.h
	$(CC) $(CFLAGS) -c $(COMMONDIR)/trace_utils.cc $(INCLUDES)

install:
	mv $(PROGS) $(BINDIR)

clean:
	rm -f $(PROGS) *.o *.a *~ *.lst *.tmp .pure *.bak *.log
