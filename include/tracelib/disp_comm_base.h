#ifndef DISP_SRC_DISP_COMM_BASE_H
#define DISP_SRC_DISP_COMM_BASE_H

#include "data_region_2d_bare_base.h"

template <typename DT>
class DISPCommBase{
  protected:
    int rank_ = -1;
    int size_ = -1;

  public:
    virtual void GlobalInPlaceCombination(DataRegion2DBareBase<DT> &dr) = 0;

    /// Accessors
    int rank() const { return rank_; }
    int size() const { return size_; }

    virtual ~DISPCommBase(){};
};

#endif    // DISP_SRC_DISP_COMM_BASE_H
