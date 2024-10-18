//#include "zhelpers.h"
#include <assert.h>
#include <stdlib.h>
extern int cleanup_mock_data();
#include <stdio.h>
#include <string.h>
#include "mock_data_acq.h"
#include "trace_streamer.h"
#include "zmq.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include "server.h"

int test_float(float *mydata, int myn, int a, int b)
{
  for(int i=0; i<myn; ++i) printf("%f\n",mydata[i]);
  printf("%d\n", a+b);
  return 0;
}

int64_t timestamp_now (void)
{
  struct timeval tv;
  gettimeofday (&tv, NULL);
  return (int64_t) tv.tv_sec * CLOCKS_PER_SEC + tv.tv_usec;
}

double timestamp_to_seconds (int64_t timestamp)
{
  return timestamp / (double) CLOCKS_PER_SEC;
}

int whatsup(){
  printf("Great whatzup with ya?\n");
  return 0;
}

/// ZeroMQ context
void *context;
void *main_worker;
int *worker_ids;
void **workers;
int n_workers;

uint64_t seq;

/// Mock data file
/// Being set at setup_mock_data()
dacq_file_t *dacq_file;


int setup_mock_data(char *fp, int nsubsets)
{
  /// Setup mock data acquisition
  /// mock_interval_t interval = { interval_sec, interval_nanosec };
  mock_interval_t interval = { 0, 0 };
  dacq_file = mock_dacq_file(fp, interval);
  if(nsubsets!=0) {
    dacq_file_t *ndacq_file = (nsubsets>0) ?
                mock_dacq_interleaved(dacq_file, nsubsets) :
                mock_dacq_interleaved_equally(dacq_file);
    mock_dacq_file_delete(dacq_file);
    dacq_file = ndacq_file;
  }

  return 0;
}

int handshake(char *bindip, int port, int row, int col)
{
  /// Figure out how many ranks there is at the remote location
  main_worker = zmq_socket(context, ZMQ_REP);
  char addr[64];
  snprintf(addr, 64, "tcp://%s:%d", bindip, port++);
  printf("binding to=%s\n", addr);
  zmq_bind(main_worker, addr);
  tomo_msg_t *msg = tracemq_recv_msg(main_worker);
  tomo_msg_data_info_req_t* info = tracemq_read_data_info_req(msg);

  /// Setup worker data structures
  n_workers = info->comm_size;
  printf("n_workers=%d\n",n_workers);
  worker_ids = (int*)malloc(n_workers*sizeof(int));
  workers = (void**)malloc(n_workers*sizeof(void*)); assert(workers!=NULL);
  worker_ids[0] = info->comm_rank;
  tracemq_free_msg(msg);

  /// Setup remaining workers' sockets
  workers[0] = main_worker; /// We already know main worker
  for(int i=1; i<n_workers; ++i){
    void *worker = zmq_socket(context, ZMQ_REP);
    workers[i] = worker;
    char addr[64];
    snprintf(addr, 64, "tcp://%s:%d", bindip, port++);
    zmq_bind(workers[i], addr);
  }

  /// Handshake with other workers
  for(int i=1; i<n_workers; ++i){
   msg = tracemq_recv_msg(workers[i]); assert(seq==msg->seq_n);
   tomo_msg_data_info_req_t* info = tracemq_read_data_info_req(msg);
   worker_ids[i]=info->comm_rank;
   tracemq_free_msg(msg);
  }
  ++seq;

  /// Distribute data info
  for(int i=0; i<n_workers; ++i){
   tomo_msg_data_info_rep_t info = assign_data(worker_ids[i], n_workers,
                                      row, col);
   tomo_msg_t *msg = tracemq_prepare_data_info_rep_msg(seq,
                         info.beg_sinogram, info.n_sinograms,
                         info.n_rays_per_proj_row, info.tn_sinograms);
   tracemq_send_msg(workers[i], msg);
   tracemq_free_msg(msg);
  }
  ++seq;

  /// recieve ready message
  for(int i=0; i<n_workers; ++i){
   msg = tracemq_recv_msg(workers[i]);
   assert(msg->type==TRACEMQ_MSG_DATA_REQ);
   assert(seq==msg->seq_n);
   tracemq_free_msg(msg);
  }
  ++seq;

  return 0;
}

/// This is a blocking function.
/// For each incoming image, this function is called
/// The image is partitioned to rows and sent to corresponding nodes
int push_image(float *data, int n, int row, int col, float theta, int id, float center)
{
  if(data == NULL) return 0;
  int dims[2] = {row, col};
  ts_proj_data_t proj = { dims, theta, id, data };

  center = (center==0.) ? proj.dims[1]/2. : center;
  printf("Sending proj: id=%d; center=%f; dims[0]=%d; dims[1]=%d; theta=%f\n",
      proj.id, center, dims[0], dims[1], theta);

  /// Default center is middle of columns
  tomo_msg_t **worker_msgs = generate_tracemq_worker_msgs(
      proj.data, proj.dims, proj.id,
      proj.theta, n_workers, center, seq);

  /// Send data to workers
  for(int i=0; i<n_workers; ++i){
    /// Prepare zmq message
    tomo_msg_t *curr_msg = worker_msgs[i];
    zmq_msg_t msg;
    int rc = zmq_msg_init_size(&msg, curr_msg->size); assert(rc==0);
    memcpy((void*)zmq_msg_data(&msg), (void*)curr_msg, curr_msg->size);
    rc = zmq_msg_send(&msg, workers[i], ZMQ_DONTWAIT);
    assert(rc==(int)curr_msg->size);
  }
  ++seq;
  /// Check if workers received their corresponding data chunks
  for (int i=0; i<n_workers; ++i) {
    tomo_msg_t *msg = tracemq_recv_msg(workers[i]);
    assert(msg->type==TRACEMQ_MSG_DATA_REQ);
    assert(msg->seq_n==seq);
    tracemq_free_msg(msg);
  }
  ++seq;
  /// Clean-up data chunks
  for(int i=0; i<n_workers; ++i)
    free(worker_msgs[i]);
  free(worker_msgs);

  return 0;
}

int done_image()
{
  /// All the projections are finished
  for(int i=0; i<n_workers; ++i){
    zmq_msg_t msg;
    tomo_msg_t msg_fin = {.seq_n=seq, .type = TRACEMQ_MSG_FIN_REP,
                          .size=sizeof(tomo_msg_t) };
    int rc = zmq_msg_init_size(&msg, sizeof(msg_fin)); assert(rc==0);
    memcpy((void*)zmq_msg_data(&msg), (void*)&msg_fin, sizeof(msg_fin));
    rc = zmq_msg_send(&msg, workers[i], 0); assert(rc==sizeof(msg_fin));
  }
  ++seq;

  // Receive worker fin reply
  for(int j=0; j<n_workers; ++j){
    tomo_msg_t *msg = tracemq_recv_msg(workers[j]);
    assert(msg->type==TRACEMQ_MSG_FIN_REP);
    assert(msg->seq_n==seq);
    tracemq_free_msg(msg);
  }
  ++seq;

  return 0;
}

int finalize_tmq()
{
  /// Cleanup resources
  for(int i=0; i<n_workers; ++i){
    zmq_close (workers[i]);
  }
  zmq_ctx_destroy (context);
  free(workers);

  return 0;
}

int init_tmq ()
{
  context = zmq_ctx_new(); /// Setup zmq
  seq=0; /// Packet sequence numbers

  return 0;
}

