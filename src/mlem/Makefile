# Compiler
CC = mpiicpc


# Directories
ROOTDIR = ../../
# Bin directory
BINDIR = ${ROOTDIR}/bin/

# Flags
CFLAGS = -O2 -Wall -Wextra -std=c++11 
CFLAGS += -g -fPIC -DTIMERON 
#CFLAGS += -no-inline-factor -no-inline-max-size  # didn't help

# Common data structures and utilities classes
COMMONDIR = ${ROOTDIR}/src/common
# DISP Framework src dir
DISPDIR = ${COMMONDIR}/DISP/core

HDF5LIB = ${HDF5_HOME}/lib
HDF5INC = ${HDF5_HOME}/include 

LIBS = -lm -lhdf5
LIBDIRS = -L$(HDF5LIB) 
INCLUDES = -I$(DISPDIR) -I${COMMONDIR} -I$(HDF5INC)

# Executable/reconstruction objects
MLEM_OBJS = mlem.o mlem_main.o
MLEM_ORDERED_OBJS = mlem.o mlem_ordered_main.o
COMMON_OBJS = trace_h5io.o trace_utils.o

# Executables
PROGS = mlem mlem_ordered

.PHONY: default clean

all: $(PROGS)

# SIRT
mlem: $(COMMON_OBJS) $(MLEM_OBJS)
	$(CC) $(CFLAGS) -o $@ $(MLEM_OBJS) $(COMMON_OBJS) $(INCLUDES) $(LIBDIRS) $(LIBS)

mlem_ordered: $(COMMON_OBJS) $(MLEM_ORDERED_OBJS)
	$(CC) $(CFLAGS) -o $@ $(MLEM_ORDERED_OBJS) $(COMMON_OBJS) $(INCLUDES) $(LIBDIRS) $(LIBS)

mlem_main.o: mlem_main.cc
	$(CC) $(CFLAGS) -c mlem_main.cc $(INCLUDES)

mlem_ordered_main.o: mlem_ordered_main.cc
	$(CC) $(CFLAGS) -c mlem_ordered_main.cc $(INCLUDES)

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
