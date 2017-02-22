#ifndef TRACE_COMMONS_STREAM_MOCKSTREAMING_H
#define TRACE_COMMONS_STREAM_MOCKSTREAMING_H

#include "trace_data.h"
#include "data_region_base.h"
#include "disp_engine_reduction.h"
#include <vector>

class TraceStream
{
  private:
    std::string dest_ip_;
    int dest_port_;
    int window_len_;
    int iteration_on_window_; 
    TraceMetadata &metadata_;

    int curr_proj_index_ = 0;
    int curr_iteration_ = 0;

    size_t ray_per_proj;
    float *curr_projs = nullptr;
    float *curr_theta = nullptr;

    std::vector<float> vproj;
    std::vector<float> vtheta;

  public:
    TraceStream(std::string dest_ip,
                int dest_port,
                int window_len, 
                int iteration_on_each_window,
                TraceMetadata &metadata);
    DataRegionBase<float, TraceMetadata>* ReadWindow();

    float* data() const { return &orig_data_[0]; }
    TraceMetadata& metadata() const { return metadata_; }

    int curr_proj_index() const { return curr_proj_index_; }
    int curr_iteration() const { return curr_iteration_; }
    void Reset() {curr_proj_index_=0; curr_iteration_=0; }
};

#endif
