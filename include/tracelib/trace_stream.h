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
    uint32_t window_len_;
    uint32_t counter_;
    TraceMQ traceMQ_;

    std::vector<float> vproj;
    std::vector<float> vtheta;
    std::vector<tomo_msg_data_t> vmeta;

    /// Add streaming message to vectors
    void AddTomoMsg(tomo_msg_data_t &msg);
    /// Erase first message
    void EraseBegTraceMsg();
    /// Generates a data region that can be processed by Trace
    DataRegionBase<float, TraceMetadata>* SetupTraceDataRegion(
      DataRegionBareBase<float> &recon_image);

    /// Getter for traceMQ
    TraceMQ& traceMQ() { return traceMQ_; }

  public:
    TraceStream(std::string dest_ip,
                int dest_port,
                uint32_t window_len, 
                int comm_rank,
                int comm_size, 
                std::string pub_info);
    TraceStream(std::string dest_ip,
                int dest_port,
                uint32_t window_len, 
                int comm_rank,
                int comm_size);

    /* Create a data region from sliding window
     * @param recon_image Initial values of reconstructed image
     * @param step        Sliding step. Waits at least step projection 
     *                    before returning window back to the reconstruction
     *                    engine
     *
     * Return:  nullptr if there is no message and sliding window is empty
     *          DataRegionBase if there is data in sliding window
     */
    DataRegionBase<float, TraceMetadata>* ReadSlidingWindow(
      DataRegionBareBase<float> &recon_image, 
      int step);

    tomo_msg_metadata_t metadata() { return traceMQ().metadata(); }
    uint32_t counter() const { return counter_; }

    void WindowLength(int wlen);

    /* Publish reconstructed slices.
     * @param slice Slice and its metadata information.
     */
    void PublishImage(DataRegionBase<float, TraceMetadata> &slice);
};

#endif // TRACE_COMMONS_STREAM_TRACE_STREAM_H
