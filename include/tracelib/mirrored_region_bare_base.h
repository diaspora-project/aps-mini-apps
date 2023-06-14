/** \file mirrored_region_bare_base.h 
 */
#ifndef DISP_SRC_DISP_MIRRORED_REGION_BARE_BASE_H
#define DISP_SRC_DISP_MIRRORED_REGION_BARE_BASE_H

/**
 * \class MirroredRegionBareBase
 * \brief Basic class that represents the mirrored regions.
 *
 * Contact: bicer@anl.gov
 */

#include <vector>
#include "data_region_a.h"

template <typename T>
class ADataRegion;

template <typename T>
class MirroredRegionBareBase {
  private:
    T * const data_;
    const size_t count_;

    const size_t index_;
    ADataRegion<T> const * const parent_;

  public:
    virtual ~MirroredRegionBareBase() {}

    explicit MirroredRegionBareBase(MirroredRegionBareBase<T> &region)
      : MirroredRegionBareBase<T>(
          region.parent_, 
          region.data_, 
          region.count_, 
          region.index_)
    {}

    explicit MirroredRegionBareBase(
        ADataRegion<T> const * const parent, 
        T * const data, size_t count, size_t index)
      : data_{data}
      , count_{count}
      , index_{index}
      , parent_{parent}
    {
      if(data == nullptr)
        throw std::invalid_argument("Data pointer cannot be null!");
      if(count<1)
        throw std::invalid_argument("Number of data items cannot be less than 1!");
      if(parent == nullptr)
        throw std::invalid_argument("Parent pointer cannot be null!");
      if(data+count > &((*parent)[parent->count()]))
        throw std::out_of_range("data+count is out of range considering parent data ptr!");
    }

    T& operator[](const size_t index) const { return data_[index]; }

    size_t count() const { return count_; }
    size_t index() const { return index_; }

    virtual MirroredRegionBareBase<T>* Clone(){
      MirroredRegionBareBase<T> *mirror = 
        new MirroredRegionBareBase<T>(parent_, data_, count_, index_);
      return mirror;
    }

};

#endif    // DISP_SRC_DISP_MIRRORED_REGION_BARE_BASE_H
