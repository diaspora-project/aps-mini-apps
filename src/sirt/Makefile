# Compiler
CC = mpicxx#/local/bin/mpicxx

# Directories
ROOTDIR = ../..
# Bin directory
BINDIR = ${ROOTDIR}/bin/

# Flags
CFLAGS = -O3 -Wall -Wextra -std=c++11 
CFLAGS += -fPIC -DTIMERON #-fno-builtin

# Common data structures and utilities classes
COMMONDIR = ${ROOTDIR}/src/common
# DISP Framework src dir
DISPDIR = ${COMMONDIR}/DISP/core
STREAMDIR = ${COMMONDIR}/stream

#HDF5LIB = ${HDF5_HOME}/lib
#HDF5INC = ${HDF5_HOME}/include

#ZMQLIB = "/home/bicer/local/lib"

LIBS = -lzmq -lpthread #-lhdf5 -lm 
LIBDIRS = -L/home/beams/TBICER/miniconda3/envs/workflow/lib #-L$(HDF5LIB)  -L${ZMQLIB}
INCLUDES = -I$(STREAMDIR) -I$(DISPDIR) -I${COMMONDIR} -I/home/beams/TBICER/miniconda3/envs/workflow/include #-I$(HDF5INC)

# Executable/reconstruction objects
SIRT_MODEL_OBJS = sirt_main_model.o
SIRT_OBJS = sirt.o sirt_stream_main.o
COMMON_OBJS = trace_utils.o trace_stream.o trace_mq.o #trace_h5io.o

# Executables
MODEL = sirt_model
PROGS = sirt_stream

.PHONY: default clean

all: $(PROGS)
model: $(MODEL)

# SIRT
sirt_stream: $(COMMON_OBJS) $(SIRT_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SIRT_OBJS) $(COMMON_OBJS) $(INCLUDES) $(LIBDIRS) $(LIBS)

sirt_stream_main.o: sirt_stream_main.cc
	$(CC) $(CFLAGS) -c sirt_stream_main.cc $(INCLUDES)

sirt_model: $(COMMON_OBJS) $(SIRT_MODEL_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SIRT_MODEL_OBJS) $(COMMON_OBJS) $(LIBDIRS) $(LIBS) 
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

trace_stream.o: $(STREAMDIR)/trace_stream.cc $(STREAMDIR)/trace_stream.h
	$(CC) $(CFLAGS) -c $(STREAMDIR)/trace_stream.cc $(INCLUDES)

trace_mq.o: $(STREAMDIR)/trace_mq.cc $(STREAMDIR)/trace_mq.h
	$(CC) $(CFLAGS) -c $(STREAMDIR)/trace_mq.cc $(INCLUDES)

install:
	mv $(PROGS) $(BINDIR)

clean:
	rm -f $(PROGS) $(MODEL) *.o *.a *~ *.lst *.tmp .pure *.bak *.log
