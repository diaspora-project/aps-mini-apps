#ifndef TRACE_COMMONS_STREAM_TRACE_MQ_H
#define TRACE_COMMONS_STREAM_TRACE_MQ_H

#include "zmq.h"

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

enum class TMQ_State { DATA, FIN };

class TraceMQ
{
  private:
    std::string dest_ip_;
    int dest_port_;
    int comm_rank_;
    int comm_size_;

    void *context;
    void *server;

    TMQ_State state_;

    tomo_msg_t* ReceiveMsgHelper();

  public:
    TraceMQ(std::string dest_ip,
            int dest_port,
            int comm_rank,
            int comm_size);
    ~TraceMQ();

    tomo_msg_t* ReceiveMsg();
    void DeleteMsg(tomo_msg_t *msg);

    TMQ_State state() const { return state_; } 
    void state(TMQ_State state) { state_ = state; } 

};

#endif // TRACE_COMMONS_STREAM_TRACE_MQ_H
