#ifndef DISP_APPS_RECONSTRUCTION_COMMON_TRACE_COMM_H_
#define DISP_APPS_RECONSTRUCTION_COMMON_TRACE_COMM_H_

#include "data_region_a.h"

namespace trace_comm {
  void GlobalNeighborUpdate(
      ADataRegion<float> &recon_a,
      int num_slices,
      size_t slice_size);
}

#endif /// DISP_APPS_RECONSTRUCTION_COMMON_TRACE_COMM_H_
