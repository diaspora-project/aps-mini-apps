/** \file mirrored_region_base.h 
 */
#ifndef DISP_SRC_DISP_MIRRORED_REGION_BASE_H
#define DISP_SRC_DISP_MIRRORED_REGION_BASE_H

/**
 * \class MirroredRegionBase
 * \brief Extended mirrored region class with metadata information.
 *
 * Contact: bicer@anl.gov
 */

#include <vector>
#include "mirrored_region_bare_base.h"


template <typename T, typename I>
class MirroredRegionBase : public MirroredRegionBareBase<T> {
  private:
    I * const metadata_;

  public:
    virtual ~MirroredRegionBase() {}

    explicit MirroredRegionBase(MirroredRegionBase<T, I> &region)
      : MirroredRegionBareBase<T>(region)
      , metadata_{region.metadata_}
    {
      if(metadata_ == nullptr)
        throw std::invalid_argument("Metadata pointer cannot be null!");
    }

    explicit MirroredRegionBase(
        ADataRegion<T> const * const parent, 
        T * const data, size_t count, size_t index, 
        I * const metadata)
      : MirroredRegionBareBase<T>(parent, data, count, index)
      , metadata_{metadata}
    {
      if(metadata == nullptr)
        throw std::invalid_argument("Metadata pointer cannot be null!");
    }

    I& metadata() const { return *metadata_; }

    virtual MirroredRegionBase<T, I>* Clone(){
      MirroredRegionBase<T, I> *mirror = 
        new MirroredRegionBase<T, I>(*this);
      return mirror;
    }
};

#endif    // DISP_SRC_DISP_MIRRORED_REGION_BASE_H
