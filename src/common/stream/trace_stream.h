#ifndef TRACE_COMMONS_STREAM_TRACE_STREAM_H
#define TRACE_COMMONS_STREAM_TRACE_STREAM_H

#include "trace_data.h"
#include "data_region_base.h"
#include "disp_engine_reduction.h"
#include "trace_mq.h"
#include <vector>

class TraceStream
{
  private:
    int window_len_;
    TraceMetadata &metadata_;
    TraceMQ traceMQ_;

    std::vector<float> vproj;
    std::vector<float> vtheta;
    std::vector<tomo_msg_t> vmeta;

    /// Add streaming message to vectors
    void AddTomoMsg(tomo_msg_t &msg);
    /// Erase first message
    void EraseBegTraceMsg();
    /// Generates a data region that can be processed by Trace
    DataRegionBase<float, TraceMetadata>* SetupTraceDataRegion();

  public:
    TraceStream(std::string dest_ip,
                int dest_port,
                int window_len, 
                TraceMetadata &metadata,
                int comm_rank,
                int comm_size);

    /* Create a data region from sliding window
     * Return:  nullptr if there is no message and sliding window is empty
     *          DataRegionBase if there is data in sliding window
     */
    DataRegionBase<float, TraceMetadata>* ReadSlidingWindow();

    TraceMetadata& metadata() const { return metadata_; }
    TraceMQ& traceMQ() const { return traceMQ_; }
};

#endif // TRACE_COMMONS_STREAM_TRACE_STREAM_H
