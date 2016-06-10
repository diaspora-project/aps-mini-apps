/** \file data_region_bare_base.h 
 */
#ifndef DISP_SRC_DISP_DATA_REGION_BARE_BASE_H
#define DISP_SRC_DISP_DATA_REGION_BARE_BASE_H

/**
 * \class DataRegionBareBase
 * \brief Basic implementation of DataRegionA class.
 *
 * Contact: bicer@anl.gov
 */

#include <iostream>
#include <vector>
#include "data_region_a.h"
#include "mirrored_region_bare_base.h"

template <typename T> 
class DataRegionBareBase : public ADataRegion<T> {
  protected:
    virtual MirroredRegionBareBase<T>* MirrorRegion(const size_t index, const size_t count)
    {
      if(index+count > ADataRegion<T>::count()) 
        throw std::out_of_range("Mirrored region is out of range!");

      MirroredRegionBareBase<T> *mirrored_region = 
        new MirroredRegionBareBase<T>(this, &ADataRegion<T>::operator[](0)+index, count, index);
      ADataRegion<T>::mirrored_regions_.push_back(mirrored_region);

      return mirrored_region;
    }

  public:

    // Constructors
    explicit DataRegionBareBase(const size_t count)
      : ADataRegion<T>(count)
    {}

    explicit DataRegionBareBase(T * const data, const size_t count)
      : ADataRegion<T>(data, count)
    {}

    DataRegionBareBase(const ADataRegion<T> &region)
      : ADataRegion<T>(region)
    {}
    DataRegionBareBase(const ADataRegion<T> &&region)
      : ADataRegion<T>(std::move(region))
    {}

    // Assignments
    DataRegionBareBase<T>& operator=(const ADataRegion<T> &region)
    {
      ADataRegion<T>::operator=(region);
    }
    DataRegionBareBase<T>& operator=(const ADataRegion<T> &&region)
    {
      ADataRegion<T>::operator=(std::move(region));
    }

    virtual MirroredRegionBareBase<T>* NextMirroredRegion(const size_t count)
    {
      if(ADataRegion<T>::index_ >= ADataRegion<T>::count()) 
        return nullptr;

      size_t num_units = 
        (ADataRegion<T>::index_+count > ADataRegion<T>::count()) ? 
          (ADataRegion<T>::count() - ADataRegion<T>::index_) : count;

      auto region = MirrorRegion(ADataRegion<T>::index_, num_units);
      ADataRegion<T>::index_ += num_units;

      return region;
    }
};

#endif    // DISP_SRC_DISP_DATA_REGION_BARE_BASE_H
