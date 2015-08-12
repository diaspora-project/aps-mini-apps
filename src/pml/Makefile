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
COMMONDIR = ${ROOTDIR}/src/common

HDF5LIB = /Users/bicer/Projects/hdf5-1.8.13/hdf5/lib
HDF5INC = /Users/bicer/Projects/hdf5-1.8.13/hdf5/include 

MPILIB = /usr/local/lib/
MPIINC = /usr/local/include/

LIBS = -lm -lhdf5 -lz -lmpi 
LIBDIRS = -L$(HDF5LIB) -L$(MPILIB)
INCLUDES = -I$(DISPDIR) -I${COMMONDIR} -I$(HDF5INC) -I$(MPIINC)

# Executable/reconstruction objects
PML_OBJS = pml.o pml_main.o
COMMON_OBJS = trace_h5io.o trace_utils.o

# Executables
PROGS = pml

.PHONY: default clean

all: $(PROGS)

# SIRT
pml: $(COMMON_OBJS) $(PML_OBJS)
	$(CC) $(CFLAGS) -o $@ $(PML_OBJS) $(COMMON_OBJS) $(INCLUDES) $(LIBDIRS) $(LIBS)
pml_main.o: pml_main.cc
	$(CC) $(CFLAGS) -c pml_main.cc $(INCLUDES)
pml.o: pml.cc
	$(CC) $(CFLAGS) -c pml.cc $(INCLUDES)

trace_h5io.o: $(COMMONDIR)/trace_h5io.cc $(COMMONDIR)/trace_h5io.h
	$(CC) $(CFLAGS) -c $(COMMONDIR)/trace_h5io.cc $(INCLUDES)

trace_utils.o: $(COMMONDIR)/trace_utils.cc $(COMMONDIR)/trace_utils.h
	$(CC) $(CFLAGS) -c $(COMMONDIR)/trace_utils.cc $(INCLUDES)

install:
	mv $(PROGS) $(BINDIR)

clean:
	rm -f $(PROGS) *.o *.a *~ *.lst *.tmp .pure *.bak *.log
