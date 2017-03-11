# Compiler
CC = mpicxx

# Directories
ROOTDIR = ../../
# Bin directory
BINDIR = ${ROOTDIR}/bin/

# Flags
CFLAGS = -O3 -Wall -Wextra -std=c++11 
CFLAGS += -fPIC -DTIMERON 
#CFLAGS += -no-inline-factor -no-inline-max-size  # didn't help

# Common data structures and utilities classes
COMMONDIR = ${ROOTDIR}/src/common
# DISP Framework src dir
DISPDIR = ${COMMONDIR}/DISP/core
STREAMDIR = ${COMMONDIR}/stream

HDF5LIB = ${HDF5_HOME}/lib
HDF5INC = ${HDF5_HOME}/include 

ZMQLIB = "/home/bicer/local/lib"

LIBS = -lm -lhdf5 -lzmq
LIBDIRS = -L$(HDF5LIB)  -L${ZMQLIB}
INCLUDES = -I$(STREAMDIR) -I$(DISPDIR) -I${COMMONDIR} -I$(HDF5INC)

# Executable/reconstruction objects
MLEM_OBJS = mlem.o mlem_stream_main.o
COMMON_OBJS = trace_h5io.o trace_utils.o trace_stream.o trace_mq.o

# Executables
PROGS = mlem_stream

.PHONY: default clean

all: $(PROGS)

mlem_stream: $(COMMON_OBJS) $(MLEM_OBJS)
	$(CC) $(CFLAGS) -o $@ $(MLEM_OBJS) $(COMMON_OBJS) $(INCLUDES) $(LIBDIRS) $(LIBS)

mlem_stream_main.o: mlem_stream_main.cc
	$(CC) $(CFLAGS) -c mlem_stream_main.cc $(INCLUDES)

mlem.o: mlem.cc
	$(CC) $(CFLAGS) -c mlem.cc $(INCLUDES)

trace_h5io.o: $(COMMONDIR)/trace_h5io.cc $(COMMONDIR)/trace_h5io.h
	$(CC) $(CFLAGS) -c $(COMMONDIR)/trace_h5io.cc $(INCLUDES)

trace_utils.o: $(COMMONDIR)/trace_utils.cc $(COMMONDIR)/trace_utils.h
	$(CC) $(CFLAGS) -c $(COMMONDIR)/trace_utils.cc $(INCLUDES)

trace_stream.o: $(STREAMDIR)/trace_stream.cc $(STREAMDIR)/trace_stream.h
	$(CC) $(CFLAGS) -c $(STREAMDIR)/trace_stream.cc $(INCLUDES)

trace_mq.o: $(STREAMDIR)/trace_mq.cc $(STREAMDIR)/trace_mq.h
	$(CC) $(CFLAGS) -c $(STREAMDIR)/trace_mq.cc $(INCLUDES)

install:
	mv $(PROGS) $(BINDIR)

clean:
	rm -f $(PROGS) *.o *.a *~ *.lst *.tmp .pure *.bak *.log
