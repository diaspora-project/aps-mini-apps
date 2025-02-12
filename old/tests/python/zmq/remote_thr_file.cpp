/*
    Copyright (c) 2007-2016 Contributors as noted in the AUTHORS file

    This file is part of libzmq, the ZeroMQ core engine in C++.

    libzmq is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License (LGPL) as published
    by the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    As a special exception, the Contributors give you permission to link
    this library with independent modules to produce an executable,
    regardless of the license terms of these independent modules, and to
    copy and distribute the resulting executable under terms of your choice,
    provided that you also meet, for each linked independent module, the
    terms and conditions of the license of that module. An independent
    module is a module which is not derived from or based on this library.
    If you modify this library, you must extend this exception to your
    version of the library.

    libzmq is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
    License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../include/zmq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>

#define MBT 1048576
#define ALIGNMENT 512
#define FILEPATH "/scratch/data.tmp"

#if defined(IOFILEENABLE)
#pragma message("fwrite/fread are being used.")
#elif defined(IODIRECTENABLE)
#pragma message("fwrite/direct-io are being used.")
#endif


#if defined(IOFILEENABLE)
int read_disk (zmq_msg_t *msg, size_t msize, FILE *f)
#elif defined(IODIRECTENABLE)
int read_disk (zmq_msg_t *msg, size_t msize, int f)
#endif
{
  char *data = (char*)memalign(ALIGNMENT, msize);

  if (!data) { 
    printf("Unable to allocate memory for data\n");
    return -1;
  }
  #if defined(IOFILEENABLE)
  int rc = fread(data, msize, 1, f);
  #elif defined(IODIRECTENABLE)
  size_t rc = read(f, data, msize);
  #endif

  #if defined(IOFILEENABLE)
  if (rc!=1) { 
  #elif defined(IODIRECTENABLE)
  if (rc!=msize) { 
  #endif
    printf("read data is not equal to expected: e=%lu; r=%lu\n", msize, rc);
    return -1;
  }

  memcpy(zmq_msg_data (msg), data, msize);
  free(data);
  return 1;
}

/// Write MB by MB
int write_disk (int fsizeMB, char *fname)
{
  FILE *f = fopen(fname, "wb");
  char *buf = (char*) calloc(MBT, 1);
  for (int i=0; i<fsizeMB; ++i) {
    int written = fwrite(buf, MBT, 1, f);
    if (written!=1){
      printf("Written size is not equal to set!\n");
      free(buf);
      fclose(f);
      return(-1);
    }
  }
  free(buf);
  fflush(f);
  fclose(f);
  return 1;
}

int main (int argc, char *argv [])
{
    const char *connect_to;
    int message_count;
    int message_size;
    void *ctx;
    void *s;
    int rc;
    int i;
    zmq_msg_t msg;
    size_t tot_size;
    void *watch, *watch_io;
    unsigned long elapsed, elapsed_io=0;
    unsigned long throughput;
    double megabits;
    char fname[128];

    if (!(argc == 4 || argc == 5)) {
        printf ("usage: remote_thr <connect-to> <message-size> "
            "<message-count> [<file-path>]\n");
        return 1;
    }
    if(argc == 4) strcpy(fname, FILEPATH);
    if(argc == 5) strcpy(fname, argv[4]);

    connect_to = argv [1];
    message_size = atoi (argv [2]);
    message_count = atoi (argv [3]);
    float msgMB = (float)message_size/(float)MBT;
    float totMB = msgMB*message_count;
    float totMBRounded = ceilf(totMB);
    int fsizeMB = int(totMBRounded);
    tot_size = (size_t)fsizeMB*MBT;
    printf("tot_size=%lu; msgMB=%.3f; totalMB=%.3f; "
      "totalMBRounded=%.3f; fsizeMB=%d, message_count=%d; message size=%d\n", 
      tot_size, msgMB, totMB, totMBRounded, fsizeMB, message_count, message_size);

    /// Create data and write it disk 
    #if defined(IOFILEENABLE) || defined(IODIRECTENABLE)
    watch = zmq_stopwatch_start ();
    write_disk (fsizeMB, fname);
    elapsed = zmq_stopwatch_stop (watch);
    printf ("write time (microsecond): %.3f\n", (double)elapsed);
    #endif

    #if defined(IOFILEENABLE)
    FILE *f = fopen(fname, "rb");
    if (!f) {
    #elif defined(IODIRECTENABLE)
    int f = open(fname, O_RDONLY | O_DIRECT);
    if (f==-1) {
    #endif
    #if defined(IOFILEENABLE) || defined(IODIRECTENABLE)
      printf("unable to open file for reading:%s\n", fname);
      return -1;
    }
    #endif

    ctx = zmq_init (1);
    if (!ctx) {
        printf ("error in zmq_init: %s\n", zmq_strerror (errno));
        return -1;
    }

    s = zmq_socket (ctx, ZMQ_PUSH);
    if (!s) {
        printf ("error in zmq_socket: %s\n", zmq_strerror (errno));
        return -1;
    }

    //  Add your socket options here.
    //  For example ZMQ_RATE, ZMQ_RECOVERY_IVL and ZMQ_MCAST_LOOP for PGM.

    rc = zmq_connect (s, connect_to);
    if (rc != 0) {
        printf ("error in zmq_connect: %s\n", zmq_strerror (errno));
        return -1;
    }


    #if defined(IOFILEENABLE) || defined(IODIRECTENABLE)
    watch = zmq_stopwatch_start ();
    #endif
    for (i = 0; i != message_count; i++) {
        rc = zmq_msg_init_size (&msg, message_size);
        if (rc != 0) {
            printf ("error in zmq_msg_init_size: %s\n", zmq_strerror (errno));
            return -1;
        }

        #if defined(IOFILEENABLE) || defined(IODIRECTENABLE)
        watch_io = zmq_stopwatch_start ();
        read_disk (&msg, message_size, f);
        elapsed_io += zmq_stopwatch_stop (watch_io);
        #endif

        rc = zmq_sendmsg (s, &msg, 0);
        if (rc < 0) {
            printf ("error in zmq_sendmsg: %s\n", zmq_strerror (errno));
            return -1;
        }
        rc = zmq_msg_close (&msg);
        if (rc != 0) {
            printf ("error in zmq_msg_close: %s\n", zmq_strerror (errno));
            return -1;
        }
    }
    elapsed = zmq_stopwatch_stop (watch);
    if (elapsed == 0)
        elapsed = 1;
    throughput = (unsigned long)
        ((double) message_count / (double) elapsed * 1000000);
    megabits = ((double) throughput * message_size * 8) / 1000000;

    printf ("message size: %d [B]\n", (int) message_size);
    printf ("message count: %d\n", (int) message_count);
    printf ("mean throughput: %d [msg/s]\n", (int) throughput);
    printf ("mean throughput: %.3f [Mb/s]\n", (double) megabits);

    #if defined(IOFILEENABLE) || defined(IODIRECTENABLE)
    printf ("read time (microsecond): %.3f\n", (double)elapsed_io);
    #endif
    #if defined(IOFILEENABLE)
    fclose(f); // open(...) does not need fclose?
    #endif

    rc = zmq_close (s);
    if (rc != 0) {
        printf ("error in zmq_close: %s\n", zmq_strerror (errno));
        return -1;
    }

    rc = zmq_ctx_term (ctx);
    if (rc != 0) {
        printf ("error in zmq_ctx_term: %s\n", zmq_strerror (errno));
        return -1;
    }

    return 0;
}
