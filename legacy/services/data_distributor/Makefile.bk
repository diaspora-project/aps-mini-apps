# Compiler
CC = cc

# Flags
CFLAGS = -std=gnu99 -O2 -Wall -Wextra
CFLAGS += -g -fPIC

TPYHOME = /home/beams/TBICER/miniconda3/envs/workflow
LIBS = -lzmq -lpython3.9
LIBDIRS = -L${TPYHOME}/lib
INCNP = -I${TPYHOME}/lib/python3.9/site-packages/numpy/core/include
INCLUDES = -I${TPYHOME}/include ${INCNP}

LDFLAGS = -shared

# Executable/reconstruction objects
SERVER_OBJS = server.o mock_data_acq.o trace_streamer.o

SWIGFILE = server.i
SWIGOBJ = $(SWIGCFILE:c=o)
SWIGCFILE = $(SWIGFILE:.i=_wrap.c)

CCSWIG = swig
SWIGFLAGS = -python
PYTHONINC = -I${TPYHOME}/include/python3.9 ${INCNP}


# Executables
PROGS = server
TARGET = _tracemq.so

.PHONY: default clean

all: $(TARGET) #$(PROGS)

server: $(COMMON_OBJS) $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJS) $(INCLUDES) $(LIBDIRS) $(LIBS)

_tracemq.so: $(COMMON_OBJS) $(SERVER_OBJS) $(SWIGOBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(LIBDIRS) $(LIBS) $(SERVER_OBJS) $(LDFLAGS) $(SWIGOBJ)

swig_server_setup: 
	$(CCSWIG) $(SWIGFLAGS) $(SWIGFILE)

server_wrap.o: swig_server_setup
	$(CC) $(CFLAGS) -c $(SWIGCFILE) $(PYTHONINC) $(INCLUDES)

server.o: server.c
	$(CC) $(CFLAGS) -c server.c $(INCLUDES)

mock_data_acq.o: mock_data_acq.c mock_data_acq.h
	$(CC) $(CFLAGS) -c mock_data_acq.c $(INCLUDES)

trace_streamer.o: trace_streamer.c trace_streamer.h
	$(CC) $(CFLAGS) -c trace_streamer.c $(INCLUDES)

clean:
	rm -f $(PROGS) $(SWIGOBJ) $(SWIGCFILE) tracemq.py tracemq.pyc *.so *.o *.a *~ *.lst *.tmp .pure *.bak *.log
	rm -rf ./__pycache__
