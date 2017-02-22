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
  int my_rank, world_size;
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  int dest_port = atoi(argv[2]);
  char addr[64];
  snprintf(addr, 64, "tcp://%s:%d", argv[1], dest_port+my_rank);
  printf("[%d] Destination address=%s\n", my_rank, addr);

  // Connect to server 
  void *context = zmq_ctx_new();
  void *server = zmq_socket(context, ZMQ_REQ);
  zmq_connect(server, addr);

  // Tell how many processes there is to server
  char rank_str[16];
  if(my_rank==0){
    snprintf(rank_str, 16, "%d", world_size);
    s_send(server, rank_str);
    char *str = s_recv(server);
    printf("Server received # of processes, replied: %s", str);
    free(str);
  }

  // Introduce yourself
  snprintf(rank_str, 16, "%d", my_rank);
  s_send(server, rank_str);

  // Check if server has any projection
  tomo_msg_t *msg;
  while((msg=trace_receive_msg(server))->type){
    // Say you received it
    char ack_msg[256];
    snprintf(ack_msg, 256, "[%d]: I received the projection=%d; beginning sinogram=%d; # sinograms=%d", 
      my_rank, msg->projection_id, msg->beg_sinogram, msg->n_sinogram);
    s_send(server, ack_msg);
    printf("%s\n", ack_msg);

    // Do something with the data
    for(int i=0; i<msg->n_sinogram; ++i){
      for(int j=0; j<msg->n_rays_per_proj_col; ++j){
        printf("%hi ", msg->data[i*msg->n_rays_per_proj_col+j]);
      }
      printf("\n");
    }

    // Clean-up message
    free(msg);
  }

  // Tell you received fin message
  char ack_msg[256];
  snprintf(ack_msg, 256, "[%d]: I received fin message: %08x", my_rank, msg->type);
  s_send(server, ack_msg);
  free(msg);

  // Cleanup
  zmq_close(server);
  zmq_ctx_destroy(context);
  
  MPI_Finalize();
}

