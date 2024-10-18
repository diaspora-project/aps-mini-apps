/** \file data_region_a.h
 */
#ifndef DISP_SRC_DISP_DATA_REGION_A_H
#define DISP_SRC_DISP_DATA_REGION_A_H

/**
 * \class ADataRegion
 * \brief Abstract DataRegion class.
 *
 * Contact: bicer@anl.gov
 */

#include <iostream>
#include <vector>
#include "mirrored_region_bare_base.h"

template <typename T>
class ADataRegion {
  private:
    /**
     * \brief Main pointer that points to the beginning of this data
     * region's data.
     */
    T *data_ = nullptr;

    /**
     * \brief Number of allocated data items.
     *
     * The data pointer #data_ points to the beginning of allocated data region,
     * and #count_ specifies the number of allocated data items.
     */
    size_t count_ = 0;


    /**
     * \brief Deletes the allocated data/mirrored regions and sets
     * #count_ and #index_ to 0.
     *
     */
    virtual void Clear(){
      //std::cout << "ADataRegion: In the clear" << std::endl;
      if(data_ != nullptr){
        delete [] data_;
        data_ = nullptr;
      }
      count_ = 0;
      for(auto &m_region : mirrored_regions_){
        delete m_region;
        m_region = nullptr;
      }
      index_ = 0;
    }


  protected:
    /**
     * \brief List of child mirrored regions.
     *
     */
    std::vector<MirroredRegionBareBase<T> const *> mirrored_regions_;

    /**
     * \brief Represents the current index for the mirrored regions.
     *
     */
    size_t index_ = 0;

    /**
     * \brief Creates a mirrored region using this class's #data_.
     *
     * This function creates a mirrored region which points to the
     * index address \p index with \p count items.
     *
     * \throws std::out_of_range if the sum of \p index and \p count is
     * larger than the last item's index.
     */
    virtual MirroredRegionBareBase<T>* MirrorRegion(const size_t index, const size_t count)=0;

    /**
     * \brief Deletes mirrored regions in #mirrored_regions_.
     */
    void DeleteMirroredRegions();

    /**
     * \brief Accessor for #index_.
     *
     * \return The following item's index that was mapped by the last
     * mirrored data region. Also see #index_.
     */
    size_t index() const { return index_; }


  public:
    /**
     * \brief Contructor for creating a data region with \p count number of items.
     * \param[in] count The number items this data region has.
     */
    explicit ADataRegion(const size_t count)
      : data_{new T[count]}
      , count_{count}
      , index_{0}
    {}

    /**
     * \brief Constructor for setting up data region with pre-allocated
     * memory region.
     *
     * This constructor overrides any existing values on this data region.
     * It does not deallocate any memory (or mirrored region) which was allocated
     * previously.
     */
    explicit ADataRegion(T * const data, const size_t count)
      : data_{data}
      , count_{count}
      , index_{0}
    {}

    /**
     * \brief Copy contructor.
     *
     * This constructor creates data region only.
     * It copies the target data/mirrored regions data.
     */
    ADataRegion<T> (const ADataRegion<T> &region);

    /**
     * \brief Copy assignment.
     *
     * Copy assignment only copies the target data/mirrored regions'
     * data. It does not copy the mirrored regions or parent regions
     * pointer.
     *
     * Copy assignment is only valid if lhs is a data region.
     */
    ADataRegion<T>& operator=(const ADataRegion<T> &region);

    /**
     * \brief Move constructor.
     *
     * Moves the data region to new data region. Also moves the mirrored regions.
     * This operation is valid only if rhs is data regions.
     */
    ADataRegion<T> (ADataRegion<T> &&region);

    /**
     * \brief Move assignment.
     *
     * Moves the data region to data region. Also moves the mirrored regions.
     * This operation is valid only if both lhs and rhs are data regions.
     */
    ADataRegion<T>& operator=(ADataRegion<T> &&region);

    /**
     * \brief Destructor.
     *
     * The memory allocated for this class is freed, i.e. #data_ and
     * #mirrored_regions_. Since mirrored regions point to the memory locations
     * pointed by #data_, no delete operation is performed on mirrored reionts'
     * #data_.
     * In other words, destructors of mirrored regions do not release memory
     * pointed by their internal #data_ pointer.
     */
    virtual ~ADataRegion(){
      Clear();
    };

    /**
     * \brief Enables access to #data_ using [] operator.
     *
     * This operator does not perform any checks while accessig the target
     * index. For controlled reads and writes see #GetItem() and #SetItem().
     */
    T& operator[](size_t index) { return data_[index]; }
    const T& operator[](size_t index) const { return data_[index]; }

    /**
     * \brief Accessor for #count_.
     *
     * \return The number of items (or columns) that was pointed by #data_.
     */
    size_t count() const { return count_; }

    /**
     * \brief Accessor for #count_.
     *
     * \return The number of items (or columns) that was pointed by #data_.
     */
    size_t num_cols() const { return count_; }

    /**
     * \brief Accessor for #count_.
     *
     * \return The number of items (or columns) that was pointed by #data_.
     */
    size_t cols() const { return count_; }

    /**
     * \brief Controlled accessor for the #data_.
     *
     * This accessor performs boundary checks while accessing requested
     * \p index address.
     *
     * \throw std::out_of_range if \p index > #count_.
     */
    T& item (size_t index) const;

    /**
     * \brief Controlled mutator for the #data_.
     *
     * Updates the \p value at target \p index.
     *
     * \throw std::out_of_range if \p index > #count_.
     */
    void item(size_t index, T &&value);

    /**
     * \brief Sets all items in this data region to \p value.
     */
    void ResetAllItems(T &value);

    /**
     * \brief Creates another #DataRegion instance with the same #data_ and #count_
     *
     * \returns Cloned data region pointer.
     */
    virtual ADataRegion<T>* Clone()=0;

    /**
     * \brief Compares the boundaries of this instance and \p region.
     *
     * If boundaries are the same returns 0; otherwise returns -1.
     */
    int CompareBoundary(ADataRegion<T> &region);

    /**
     * \brief Size of the data object.
     *
     */
    size_t size() const;

    /**
     * \brief Creates a mirrored region from this data region and
     * returns its pointer.
     *
     * This function creates a new mirrored region starting from #index_
     * offset address with \p count number of items. After creating the
     * mirrored region, it resets the #index_ and adds the newly created
     * mirrored region's pointer to #mirrored_regions_ vector.
     *
     * If the \p count is not available, e.g. #index_ + \p count
     * is larger than #count_, then it returns the maximum number of items as
     * it can. If #index_ points to the end of the data region, i.e.
     * #index_ == #count_, then \p nullptr is returned.
     */
    virtual MirroredRegionBareBase<T>* NextMirroredRegion(const size_t count)=0;

    /**
     * \brief Resets the #index_ to 0, and deletes #mirrored_regions_
     * using #DeleteMirroredRegions().
     */
    void ResetMirroredRegionIter();
};


/** Constructors & Assignments */
template <typename T>
ADataRegion<T>::ADataRegion(const ADataRegion<T> &region)
  : data_{new T[region.count_]}
  , count_{region.count_}
  , index_{0}
{
  std::copy(region.data_, region.data_ + region.count_, data_);
}

template <typename T>
ADataRegion<T>::ADataRegion (ADataRegion<T> &&region)
  : data_{nullptr}
  , count_{0}
  , index_{0}
{
  *this = std::move(region);
}

template <typename T>
ADataRegion<T>& ADataRegion<T>::operator=(const ADataRegion<T> &region)
{
  if(this != &region) {
    if(region.count_ != count_){
      Clear();
      data_ = new T[region.count_];
    }
    std::copy(region.data_, region.data_ + region.count_, data_);

    count_ = region.count_;
  }
  return *this;
}

template <typename T>
ADataRegion<T>& ADataRegion<T>::operator=(ADataRegion<T> &&region)
{
  if(this != &region){
    Clear();
    data_ = region.data_;
    count_ = region.count_;

    mirrored_regions_ = std::move(region.mirrored_regions_);

    region.data_ = nullptr;
    region.count_ = 0;
  }
  return *this;
}
/** End of constructors and assingments */

template <typename T>
void ADataRegion<T>::ResetMirroredRegionIter(){
  index_ = 0;
  DeleteMirroredRegions();
}

template <typename T>
int ADataRegion<T>::CompareBoundary(ADataRegion<T> &region){
  return (region.count() != count_) ? -1 : 0;
}

template <typename T>
size_t ADataRegion<T>::size() const {
  return count_*sizeof(T);
}

template <typename T>
T& ADataRegion<T>::item (size_t index) const {
  if(index >= count_)
    throw std::out_of_range("Tried to access out of range index!");
  return data_[index];
}

template <typename T>
void ADataRegion<T>::item(size_t index, T &&value){
  if(index >= count_)
    throw std::out_of_range("Tried to update out of range index!");
  data_[index] = value;
}

template <typename T>
void ADataRegion<T>::ResetAllItems(T &value){
  for(size_t i=0; i<count_; i++)
    data_[i] = value;
}

template <typename T>
void ADataRegion<T>::DeleteMirroredRegions(){
  for(auto &m_region : mirrored_regions_){
    delete m_region;
    m_region = nullptr;
  }
  mirrored_regions_.clear();
}


#endif    // DISP_SRC_DISP_DATA_REGION_H
