# Compiler
CC = mpicxx

# Directories
ROOTDIR = ../..
# Bin directory
BINDIR = ${ROOTDIR}/bin/


# Flags
CFLAGS = -O3 -Wall -Wextra -std=c++11 
CFLAGS += -no-prec-div -ansi-alias -no-inline-factor -no-inline-max-size -g -fPIC -DTIMERON #-qopt-report=4 -qopt-report-phase ipo #no-inline-factor

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
PML_OBJS = pml.o pml_main_model.o #pml_main.o
COMMON_OBJS = trace_h5io.o trace_utils.o trace_comm.o

# Executables
PROGS = pml_model #pml

.PHONY: default clean

all: $(PROGS)

# pml
pml_model: $(COMMON_OBJS) $(PML_OBJS)
	$(CC) $(CFLAGS) -o $@ $(PML_OBJS) $(COMMON_OBJS) $(LIBDIRS) $(LIBS) 

pml: $(COMMON_OBJS) $(PML_OBJS)
	$(CC) $(CFLAGS) -o $@ $(PML_OBJS) $(COMMON_OBJS) $(LIBDIRS) $(LIBS) 

pml.o: pml.cc pml.h
	$(CC) $(CFLAGS) -c pml.cc $(INCLUDES)

pml_main_model.o: pml_main_model.cc
	$(CC) $(CFLAGS) -c pml_main_model.cc $(INCLUDES)

pml_main.o: pml_main.cc
	$(CC) $(CFLAGS) -c pml_main.cc $(INCLUDES)

trace_comm.o: $(COMMONDIR)/trace_comm.cc $(COMMONDIR)/trace_comm.h
	$(CC) $(CFLAGS) -c $(COMMONDIR)/trace_comm.cc $(INCLUDES)

trace_h5io.o: $(COMMONDIR)/trace_h5io.cc $(COMMONDIR)/trace_h5io.h
	$(CC) $(CFLAGS) -c $(COMMONDIR)/trace_h5io.cc $(INCLUDES)

trace_utils.o: $(COMMONDIR)/trace_utils.cc $(COMMONDIR)/trace_utils.h
	$(CC) $(CFLAGS) -c $(COMMONDIR)/trace_utils.cc $(INCLUDES)

install:
	mv $(PROGS) $(BINDIR)

clean:
	rm -f $(PROGS) *.o *.a *~ *.lst *.tmp .pure *.bak *.log
