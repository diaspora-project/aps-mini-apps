#ifndef TRACE_COMMONS_STREAM_TRACE_MQ_H
#define TRACE_COMMONS_STREAM_TRACE_MQ_H

#include <string>
#include <iostream>
#include "zmq.h"

#define TRACEMQ_MSG_FIN_REP       0x00000000
#define TRACEMQ_MSG_DATAINFO_REQ  0x00000001
#define TRACEMQ_MSG_DATAINFO_REP  0x00000002

#define TRACEMQ_MSG_DATA_REQ      0x00000010
#define TRACEMQ_MSG_DATA_REP      0x00000020


struct _tomo_msg_h_str {
  uint64_t seq_n;
  uint64_t type;
  uint64_t size;
  char data[];
};

struct _tomo_msg_data_info_rep_str {
  uint32_t tn_sinograms; 
	uint32_t beg_sinogram;
  uint32_t n_sinograms;
  uint32_t n_rays_per_proj_row;
};

struct _tomo_msg_data_info_req_str {
  uint32_t comm_rank; 
  uint32_t comm_size;
};

/* Center, tn_sinogram, n_rays_per_proj_row are all global and can be sent only once.
 */
struct _tomo_msg_data_str {
	int projection_id;        // projection id
	float theta;              // theta value of this projection
	float center;               // center of the projecion
	float data[];             // real projection data
	// number of rays in data=n_sinogram*n_rays_per_proj_row (n_sinogram*n_rays_per_proj_row were given in req msg.)
};

typedef struct _tomo_msg_h_str tomo_msg_t;
typedef struct _tomo_msg_data_str tomo_msg_data_t;
typedef struct _tomo_msg_data_info_req_str tomo_msg_data_info_req_t;
typedef struct _tomo_msg_data_info_rep_str tomo_msg_data_info_rep_t;
typedef struct _tomo_msg_data_info_rep_str tomo_msg_metadata_t;

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

    /* State of the TraceMQ
     *
     * DATA: Expecting data state
     * FIN: No more data is expected
     */
    TMQ_State state_;

    uint64_t seq_;

    tomo_msg_metadata_t metadata_;

    //tomo_msg_t* prepare_data_req_msg(uint64_t seq_n);
    //tomo_msg_t* prepare_data_rep_msg(uint64_t seq_n, int projection_id,
    //                                 float theta, float center,
    //                                 uint64_t data_size, float *data);

    //tomo_msg_t* prepare_data_info_rep_msg(uint64_t seq_n, 
    //                                      int beg_sinogram, int n_sinograms,
    //                                      int n_rays_per_proj_row,
    //                                      uint64_t tn_sinograms);
    //tomo_msg_t* prepare_data_info_req_msg(uint64_t seq_n, 
    //                                      uint32_t comm_rank, 
    //                                      uint32_t comm_size);
    //tomo_msg_t* prepare_fin_msg(uint64_t seq_n);

    //void send_msg(void *server, tomo_msg_t* msg);
    //tomo_msg_t* recv_msg(void *server);

    //tomo_msg_data_info_rep_t* read_data_info_rep(tomo_msg_t *msg);
    //void print_data_info_rep_msg(tomo_msg_data_info_rep_t *msg);
    //void print_data(tomo_msg_data_t *msg, size_t data_count);
    //tomo_msg_data_info_req_t* read_data_info_req(tomo_msg_t *msg);

    /** 
    This is a helper function for setting parameters in tomo_msg header.
      
    @param msg_h Pointer to header structure.
    @param seq_n Sequence number of the message.
    @param type One of the TRACEMQ_MSG_* types.
    */
    void setup_msg_header(tomo_msg_t *msg_h, uint64_t seq_n, uint64_t type, 
                          uint64_t size);

    /**
    Helper functions for preparing data request message.
    */
    tomo_msg_t* prepare_data_req_msg(uint64_t seq_n);
    tomo_msg_t* prepare_data_rep_msg(uint64_t seq_n, int projection_id,        
                                     float theta, float center, 
                                     uint64_t data_size, float *data);

    /**
    Helper functions for preparing data reply message.
    */
    tomo_msg_t* prepare_data_info_rep_msg(uint64_t seq_n, int beg_sinogram, 
                                          int n_sinograms, 
                                          int n_rays_per_proj_row,
                                          uint64_t tn_sinograms);
    tomo_msg_t* prepare_data_info_req_msg(uint64_t seq_n, uint32_t comm_rank, 
                                          uint32_t comm_size);

    /**
    Message that shows the finalization of the data acquisition.
    */
    tomo_msg_t* prepare_fin_msg(uint64_t seq_n);

    /**
    Helper functions for converting tomo_msg_t to data_info_rep/req messages.
    */
    tomo_msg_data_info_rep_t* read_data_info_rep(tomo_msg_t *msg);
    tomo_msg_data_info_req_t* read_data_info_req(tomo_msg_t *msg);

    /**
    Helper functions for printing data and info messages.
    */
    void print_data_info_rep_msg(tomo_msg_data_info_rep_t *msg);
    void print_data(tomo_msg_data_t *msg, size_t data_count);

    /**
    Sender and received functions for tracemq
    */
    void send_msg(void *server, tomo_msg_t* msg);
    tomo_msg_t* recv_msg(void *server);


  public:
    /**
    Connects to the destination host with given port. Also sets the comm_rank 
    and comm_size information in the class. 
    */
    TraceMQ(std::string dest_ip,
            int dest_port,
            int comm_rank,
            int comm_size);
    ~TraceMQ();

    /**
    Initializes the connection with the data acquisition machine or its 
    forwarder.

    Performs initial handshake with the destination host and ip address.
    First, it exchanges its comm_rank and comm_size parameters with the
    destination. Then, it receives the projection information and sets its
    local metadata structure, tomo_msg_metadata_t.
    */
    void Initialize();

    /// Waits for tomo_msg_t and returns it
    tomo_msg_t* ReceiveMsg();
    tomo_msg_data_t* read_data(tomo_msg_t *msg);

    /** Clean tracemq messages.  */
    void free_msg(tomo_msg_t *msg);

    TMQ_State state() const { return state_; } 
    void state(TMQ_State state) { state_ = state; } 

    tomo_msg_metadata_t metadata() const { return metadata_; }
    void metadata(tomo_msg_metadata_t metadata) { metadata_ = metadata; }

};

#endif // TRACE_COMMONS_STREAM_TRACE_MQ_H
