# Compiler
CC = mpiicpc

# Directories
ROOTDIR = ../..
# Bin directory
BINDIR = ${ROOTDIR}/bin/


# Flags
CFLAGS = -O3 -Wall -Wextra -std=c++11 
CFLAGS += -g -fPIC -DTIMERON 
#CFLAGS += -fast #didn't work, compilation error
#CFLAGS += -falign-functions=16 -falign-loops=16 # didn't help
#CFLAGS += -no-prec-div # didn't help
#CFLAGS += -no-inline-factor -no-inline-max-size  # didn't help
#CFLAGS += -no-prec-div -ansi-alias 

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
SIRT_MODEL_OBJS = sirt_main_model.o
SIRT_OBJS = sirt_main.o
COMMON_OBJS = sirt.o trace_h5io.o trace_utils.o

# Executables
MODEL = sirt_model
PROGS = sirt

.PHONY: default clean

all: $(PROGS)
model: $(MODEL)

# SIRT
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

install:
	mv $(PROGS) $(BINDIR)

clean:
	rm -f $(PROGS) $(MODEL) *.o *.a *~ *.lst *.tmp .pure *.bak *.log
