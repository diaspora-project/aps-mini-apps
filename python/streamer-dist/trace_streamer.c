#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "zmq.h"
#include "trace_streamer.h"

void tracemq_free_msg(tomo_msg_t *msg) {
  free(msg);
  msg=NULL;
}

void tracemq_setup_msg_header(
  tomo_msg_t *msg_h, 
  uint64_t seq_n, uint64_t type, uint64_t size)
{
  msg_h->seq_n = seq_n;
  msg_h->type = type;
  msg_h->size = size;
}

tomo_msg_t* tracemq_prepare_data_req_msg(uint64_t seq_n)
{
  size_t tot_msg_size = sizeof(tomo_msg_t);
  tomo_msg_t *msg = (tomo_msg_t *)malloc(tot_msg_size);
  tracemq_setup_msg_header(msg, seq_n, TRACEMQ_MSG_DATA_REQ, tot_msg_size);

  return msg;
}

tomo_msg_t* tracemq_prepare_data_rep_msg( uint64_t seq_n, int projection_id, 
                                          float theta, float center, 
                                          uint64_t data_size, float *data)
{
  uint64_t tot_msg_size=sizeof(tomo_msg_t)+sizeof(tomo_msg_data_t)+data_size;
  tomo_msg_t *msg_h = (tomo_msg_t *)malloc(tot_msg_size);
  tomo_msg_data_t *msg = (tomo_msg_data_t *) msg_h->data;
  tracemq_setup_msg_header(msg_h, seq_n, TRACEMQ_MSG_DATA_REP, tot_msg_size);

  msg->projection_id = projection_id;
  msg->theta = theta;
  msg->center = center;
  memcpy(msg->data, data, data_size);
  
  printf("theta=%f\n", theta);

  return msg_h;
}

tomo_msg_data_t* tracemq_read_data(tomo_msg_t *msg){
  return (tomo_msg_data_t *) msg->data;
}

void tracemq_print_data(tomo_msg_data_t *msg, size_t data_count){
  printf("projection_id=%u; theta=%f; center=%f\n", 
    msg->projection_id, msg->theta, msg->center);
  for(size_t i=0; i<data_count; ++i)
    printf("%f ", msg->data[i]);
  printf("\n");
}

tomo_msg_t* tracemq_prepare_data_info_rep_msg(uint64_t seq_n, 
                                              int beg_sinogram, int n_sinograms,
                                              int n_rays_per_proj_row,
                                              uint64_t tn_sinograms)
{
  uint64_t tot_msg_size = sizeof(tomo_msg_t)+sizeof(tomo_msg_data_info_rep_t);
  tomo_msg_t *msg = (tomo_msg_t *)malloc(tot_msg_size);
  tracemq_setup_msg_header(msg, seq_n, TRACEMQ_MSG_DATAINFO_REP, tot_msg_size);

  tomo_msg_data_info_rep_t *info = (tomo_msg_data_info_rep_t *) msg->data;
  info->tn_sinograms = tn_sinograms;
  info->beg_sinogram = beg_sinogram;
  info->n_sinograms = n_sinograms;
  info->n_rays_per_proj_row = n_rays_per_proj_row;

  return msg;
}
tomo_msg_data_info_rep_t* tracemq_read_data_info_rep(tomo_msg_t *msg){
  return (tomo_msg_data_info_rep_t *) msg->data;
}
void tracemq_print_data_info_rep_msg(tomo_msg_data_info_rep_t *msg){
  printf("Total # sinograms=%u; Beginning sinogram id=%u;"
          "# assigned sinograms=%u; # rays per projection row=%u\n", 
          msg->tn_sinograms, msg->beg_sinogram, msg->n_sinograms, msg->n_rays_per_proj_row);
}

tomo_msg_t* tracemq_prepare_data_info_req_msg(uint64_t seq_n, uint32_t comm_rank, uint32_t comm_size){
  uint64_t tot_msg_size = sizeof(tomo_msg_t)+sizeof(tomo_msg_data_info_req_t);
  printf("total message size=%zu\n", tot_msg_size);
  tomo_msg_t *msg = (tomo_msg_t *)malloc(tot_msg_size);
  tracemq_setup_msg_header(msg, seq_n, TRACEMQ_MSG_DATAINFO_REQ, tot_msg_size);

  tomo_msg_data_info_req_t *info = (tomo_msg_data_info_req_t *) msg->data;
  info->comm_rank = comm_rank;
  info->comm_size = comm_size;

  return msg;
}
tomo_msg_data_info_req_t* tracemq_read_data_info_req(tomo_msg_t *msg){
  return (tomo_msg_data_info_req_t *) msg->data;
}

tomo_msg_t* tracemq_prepare_fin_msg(uint64_t seq_n){
  uint64_t tot_msg_size = sizeof(tomo_msg_t);
  tomo_msg_t *msg = (tomo_msg_t *) malloc(tot_msg_size);
  tracemq_setup_msg_header(msg, seq_n, TRACEMQ_MSG_FIN_REP, tot_msg_size);

  return msg;
}

void tracemq_send_msg(void *server, tomo_msg_t* msg){
  zmq_msg_t zmsg;
  int rc = zmq_msg_init_size(&zmsg, msg->size); assert(rc==0);
  memcpy((void*)zmq_msg_data(&zmsg), (void*)msg, msg->size);
  rc = zmq_msg_send(&zmsg, server, 0); assert(rc==(int)msg->size);
}

tomo_msg_t* tracemq_recv_msg(void *server){
  zmq_msg_t zmsg;
  int rc = zmq_msg_init(&zmsg); assert(rc==0);
  rc = zmq_msg_recv(&zmsg, server, 0); assert(rc!=-1);
  /// Message size and calculated total message size needst to be the same
  /// FIXME?: We put tomo_msg_t.size to calculate zmq message size before it is
  /// being sent. It is being only being used for sanity check at the receiver
  /// side. 
  //printf("zmq_msg_size(&zmsg)=%zu; ((tomo_msg_t*)&zmsg)->size=%zu", zmq_msg_size(&zmsg), ((tomo_msg_t*)&zmsg)->size);
  //assert(zmq_msg_size(&zmsg)==((tomo_msg_t*)&zmsg)->size);

  tomo_msg_t *msg = (tomo_msg_t *) malloc(((tomo_msg_t*)zmq_msg_data(&zmsg))->size);
  /// Zero-copy would have been better
  memcpy(msg, zmq_msg_data(&zmsg), zmq_msg_size(&zmsg));
  zmq_msg_close(&zmsg);

  return msg;
}

tomo_msg_data_info_rep_t assign_data( uint32_t comm_rank, int comm_size, 
                                      int tot_sino, int tot_cols)
{
  uint32_t nsino = tot_sino/comm_size;
  uint32_t remaining = tot_sino%comm_size;

  int r = (comm_rank<remaining) ? 1 : 0;
  int my_nsino = r+nsino;
  int beg_sino = (comm_rank<remaining) ? (1+nsino)*comm_rank : 
                                        (1+nsino)*remaining + nsino*(comm_rank-remaining);
  
  tomo_msg_data_info_rep_t info_rep;
  info_rep.tn_sinograms = tot_sino;
  info_rep.beg_sinogram = beg_sino;
  info_rep.n_sinograms = my_nsino;
  info_rep.n_rays_per_proj_row = tot_cols;

  return info_rep;
}


tomo_msg_t** generate_tracemq_worker_msgs(float *data, int dims[], int data_id,
                                          float theta, int n_ranks, float center,
                                          uint64_t seq)
{
  int nsin = dims[0]/n_ranks;
  int remaining = dims[0]%n_ranks;

  tomo_msg_t **msgs = (tomo_msg_t **) malloc(n_ranks*sizeof(tomo_msg_t*));

  int curr_sinogram_id = 0;
  for(int i=0; i<n_ranks; ++i){
    int r = ((remaining--) > 0) ? 1 : 0;
    size_t data_size = sizeof(*data)*(nsin+r)*dims[1];
    tomo_msg_t *msg = tracemq_prepare_data_rep_msg(seq, 
                              data_id, theta, center, data_size, 
                              data+curr_sinogram_id*dims[1]);
    msgs[i] = msg;
    curr_sinogram_id += (nsin+r);
  }
  return msgs;
}
