#ifndef _TRACE_STREAMER_H
#define _TRACE_STREAMER_H

#define TRACEMQ_MSG_FIN_REP       0x00000000
#define TRACEMQ_MSG_DATAINFO_REQ  0x00000001
#define TRACEMQ_MSG_DATAINFO_REP  0x00000002

#define TRACEMQ_MSG_DATA_REQ      0x00000010
#define TRACEMQ_MSG_DATA_REP      0x00000020

#include <stdint.h>
#include <stddef.h>

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

void tracemq_free_msg(tomo_msg_t *msg);
void tracemq_setup_msg_header(tomo_msg_t *msg_h, uint64_t seq_n, uint64_t type,
                              uint64_t size);
tomo_msg_t* tracemq_prepare_data_req_msg(uint64_t seq_n);
tomo_msg_t* tracemq_prepare_data_rep_msg(uint64_t seq_n, int projection_id,
                                         float theta, float center,
                                         uint64_t data_size, float *data);
tomo_msg_data_t* tracemq_read_data(tomo_msg_t *msg);
void tracemq_print_data(tomo_msg_data_t *msg, size_t data_count);
tomo_msg_t* tracemq_prepare_data_info_rep_msg(uint64_t seq_n, 
                                              int beg_sinogram, int n_sinograms,
                                              int n_rays_per_proj_row,
                                              uint64_t tn_sinograms);
tomo_msg_data_info_rep_t* tracemq_read_data_info_rep(tomo_msg_t *msg);
void tracemq_print_data_info_rep_msg(tomo_msg_data_info_rep_t *msg);
tomo_msg_t* tracemq_prepare_data_info_req_msg(uint64_t seq_n, 
                                              uint32_t comm_rank, 
                                              uint32_t comm_size);
tomo_msg_data_info_req_t* tracemq_read_data_info_req(tomo_msg_t *msg);
tomo_msg_t* tracemq_prepare_fin_msg(uint64_t seq_n);
void tracemq_send_msg(void *server, tomo_msg_t* msg);
tomo_msg_t* tracemq_recv_msg(void *server);



tomo_msg_data_info_rep_t assign_data( uint32_t comm_rank, int comm_size, 
                                      int tot_sino, int tot_cols);
tomo_msg_t** generate_tracemq_worker_msgs(float *data, int dims[], int data_id,
                                          float theta, int n_ranks, 
                                          float center, uint64_t seq);

#endif  // _TRACE_STREAMER_H
