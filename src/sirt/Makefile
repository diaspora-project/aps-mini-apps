# Compiler
CC = mpicxx

# Directories
ROOTDIR = ../..
# DISP Framework src dir
DISPDIR = ${HOME}/projects/DISP/src/core

# Bin directory
BINDIR = ${ROOTDIR}/bin/

# Flags
CFLAGS = -std=c++11 -DAFFINITY_OFF -g -O3 -Wall -Wextra 

# Common data structures and utilities classes
COMMONDIR = ${ROOTDIR}/src/common

HDF5LIB = ${HDF5_HOME}/lib
HDF5INC = ${HDF5_HOME}/include

MPILIB = ${MPI_HOME}/lib
MPIINC = ${MPI_HOME}/inc

LIBS = -lm -lhdf5 -lz -lmpi 
LIBDIRS = -L$(HDF5LIB) -L$(MPILIB)
INCLUDES = -I$(DISPDIR) -I${COMMONDIR} -I$(HDF5INC) -I$(MPIINC)

# Executable/reconstruction objects
SIRT_OBJS = sirt.o sirt_main_model.o #sirt_main.o
COMMON_OBJS = trace_h5io.o trace_utils.o

# Executables
PROGS = sirt #sirt_model #sirt

.PHONY: default clean

all: $(PROGS)

# SIRT
sirt_model: $(COMMON_OBJS) $(SIRT_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SIRT_OBJS) $(COMMON_OBJS) $(LIBDIRS) $(LIBS) 

sirt: $(COMMON_OBJS) $(SIRT_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SIRT_OBJS) $(COMMON_OBJS) $(LIBDIRS) $(LIBS) 

sirt.o: sirt.cc sirt.h
	$(CC) $(CFLAGS) -c sirt.cc $(INCLUDES)

sirt_main_model.o: sirt_main_model.cc
	$(CC) $(CFLAGS) -c sirt_main_model.cc $(INCLUDES)

sirt_main.o: sirt_main.cc
	$(CC) $(CFLAGS) -c sirt_main.cc $(INCLUDES)

trace_h5io.o: $(COMMONDIR)/trace_h5io.cc $(COMMONDIR)/trace_h5io.h
	$(CC) $(CFLAGS) -c $(COMMONDIR)/trace_h5io.cc $(INCLUDES)

trace_utils.o: $(COMMONDIR)/trace_utils.cc $(COMMONDIR)/trace_utils.h
	$(CC) $(CFLAGS) -c $(COMMONDIR)/trace_utils.cc $(INCLUDES)

install:
	mv $(PROGS) $(BINDIR)

clean:
	rm -f $(PROGS) *.o *.a *~ *.lst *.tmp .pure *.bak *.log
