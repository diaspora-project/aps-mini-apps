#include "trace_mq.h"

/* Center, tn_sinogram, n_rays_per_proj_row are all global and can be sent only once.
 */
struct _tomo_msg_str {
  uint32_t type;
  int projection_id;        // projection id
  float theta;              // theta value of this projection
  int beg_sinogram;         // beginning sinogram id
  int tn_sinogram;          // Total # Sinograms
  int n_sinogram;           // # Sinograms
  // int nprojs=1;          // Always projection by projection
  int n_rays_per_proj_row;  // # rays per projection row 
  int center;               // center of the projecion
  float data[];             // real projection data
  // number of rays in data=n_sinogram*n_rays_per_proj_row
};
typedef struct _tomo_msg_str tomo_msg_t;


TraceMQ::TraceMQ( 
  std::string dest_ip, int dest_port, int comm_rank, int comm_size) : 
    dest_ip_ {dest_ip}, 
    dest_port_ {dest_port}, 
    comm_rank_ {comm_rank}, 
    comm_size_ {comm_size},
    state_ {TMQ_State::DATA}
{
  std::string addr("tcp://" + dest_ip_ + ":" + std::to_string(dest_port+comm_rank));
  std::cout << "[" << comm_rank << "] Destination address=" << addr << std::endl;

  context = zmq_ctx_new();
  server = zmq_socket(context, ZMQ_REQ); 
  zmq_connect(server, addr.c_str()); 

  /// Tell how many processes there is to server
  if(comm_rank==0){
    std::string rank_str(std::to_string(comm_size));
    s_send(server, rank_str.c_str());
    char *str = s_recv(server);
    std::printf("Server received # of processes, replied: %s", str);
    free(str);
  }

  /// Introduce yourself
  std::string rank_str(std::to_string(comm_rank));
  s_send(server, rank_str.c_str());
}

tomo_msg_t* TraceMQ::ReceiveMsgHelper(){
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


tomo_msg_t* TraceMQ::ReceiveMsg() {
  /// Check if server has any projection
  if(state()==TMQ_State::FIN) return nullptr;
  tomo_msg_t *msg = ReceiveMsgHelper(server);
  if(msg->type) { /// Message has data
    /// Say you received it
    std::string ack_msg("ack::received:" + std::to_string(comm_rank));
    s_send(server, ack_msg.c_str());
    state(TMQ_State::DATA);
  } else { /// Message is for finalization
    std::string fin_msg("fin::received:" + std::to_string(comm_rank));
    s_send(server, fin_msg);
    state(TMQ_State::FIN);
  }
}

void TraceMQ::DeleteMsg(tomo_msg_t *msg) {
  if(msg!=NULL) { free(msg); msg=NULL; }
}

TraceMQ::~TraceMQ() {
  zmq_close(server);
  zmq_ctx_destroy(context);
}
