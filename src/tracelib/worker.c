//   Request worker in C

#include "zhelpers.h"
#include "mpi.h"
#include "streamer.h"

tomo_msg_t* trace_receive_msg(void *server){
    zmq_msg_t zmsg;
    int rc = zmq_msg_init(&zmsg); assert(rc==0);
    rc = zmq_msg_recv(&zmsg, server, 0); assert(rc!=-1);

    size_t data_size = zmq_msg_size(&zmsg)-(sizeof(tomo_msg_t));
    tomo_msg_t *msg = malloc(sizeof(*msg)+data_size);
    // Zero-copy would have been better
    memcpy(msg, zmq_msg_data(&zmsg), zmq_msg_size(&zmsg));
    zmq_msg_close(&zmsg);

    return msg;
}

int main (int argc, char *argv[])
{
  if(argc!=3) {
    printf("Usage: %s <dest-ip-address=164.54.143.3> <dest-port=5560>\n", argv[0]);
    exit(0);
  }

  // Setup MPI
  int comm_rank, comm_size;
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);

  int dest_port = atoi(argv[2]);
  char addr[64];
  snprintf(addr, 64, "tcp://%s:%d", argv[1], dest_port+comm_rank);
  printf("[%d] Destination address=%s\n", comm_rank, addr);

  // Connect to server 
  void *context = zmq_ctx_new();
  void *server = zmq_socket(context, ZMQ_REQ);
  zmq_connect(server, addr);

  /// Handshake with server
  uint64_t seq = 0;
  tomo_msg_t *msg = tracemq_prepare_data_info_req_msg(seq, comm_rank, comm_size);
  tracemq_send_msg(server, msg);
  tracemq_free_msg(msg);
  ++seq;

  /// Receive data info msg
  msg = tracemq_recv_msg(server); assert(seq==msg->seq_n);
  tomo_msg_data_info_rep_t *info = tracemq_read_data_info_rep(msg);
  tracemq_print_data_info_rep_msg(info);
  //uint32_t tn_sinogram=info->tn_sinograms;
  uint32_t n_sinogram=info->n_sinograms;
  //uint32_t beg_sinogram=info->beg_sinogram;
  uint32_t n_rays_per_proj_row=info->n_rays_per_proj_row;
  // ... setup trace data structures ...
  tracemq_free_msg(msg);
  ++seq;

  /// Ready to receive msgs
  msg = tracemq_prepare_data_req_msg(seq);
  tracemq_send_msg(server, msg);
  tracemq_free_msg(msg);
  ++seq;

  tomo_msg_t *dmsg;
  while((dmsg=tracemq_recv_msg(server))->type & TRACEMQ_MSG_DATA_REP){
    assert(seq==dmsg->seq_n);
    ++seq;

    /// Right after receiving message, tell server that you received it
    msg = tracemq_prepare_data_req_msg(seq);
    tracemq_send_msg(server, msg);
    tracemq_free_msg(msg);
    ++seq;

    tomo_msg_data_t *data_msg = tracemq_read_data(dmsg);
    // ... do something with the data ...
    tracemq_print_data(data_msg, n_sinogram*n_rays_per_proj_row);
    tracemq_free_msg(dmsg);
  }

  assert(dmsg->type==TRACEMQ_MSG_FIN_REP); /// Last message must be fin
  printf("seq_n=%zu; seq=%llu\n", dmsg->seq_n, seq);
  assert(dmsg->seq_n==seq);
  tracemq_free_msg(dmsg);
  ++seq;

  // send fin received msg
  msg = malloc(sizeof(tomo_msg_t));
  msg->type=TRACEMQ_MSG_FIN_REP;
  msg->seq_n=seq;
  msg->size=sizeof(tomo_msg_t);
  tracemq_send_msg(server, msg);
  tracemq_free_msg(msg);
  ++seq;

  // Cleanup
  zmq_close(server);
  zmq_ctx_destroy(context);
  
  MPI_Finalize();
}

