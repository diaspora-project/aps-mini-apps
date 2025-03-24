#ifndef DISP_SRC_DISP_ENGINE_BASE_H_
#define DISP_SRC_DISP_ENGINE_BASE_H_

#include <sched.h>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <chrono>
#include "data_region_a.h"
// #include "disp_comm_base.h"
#include "reduction_space_a.h"

enum ReplicationTypes{
  FULL_REPLICATION,
  REPLICATED,
  SINGLE
};

template <typename RST, typename DT>
class DISPEngineBase{
  protected:
    std::vector<AReductionSpaceBase<RST, DT> *> reduction_spaces_;

    int num_reduction_threads_;
    int num_procs_;
    std::mutex partitioner_mutex_;
    ReplicationTypes replication_type_;

    // DISPCommBase<DT> *comm_;

    virtual void ReductionWrapper(AReductionSpaceBase<RST, DT> &reduction_space,
        ADataRegion<DT> &input_data, int &req_units)=0;

    virtual MirroredRegionBareBase<DT>* PartitionWrapper(ADataRegion<DT> &input_data, 
        int req_units)=0;
    virtual MirroredRegionBareBase<DT>* Partitioner(ADataRegion<DT> &input_data, 
        int req_units)=0;

    virtual void SeqInPlaceLocalSynch(
        std::vector<AReductionSpaceBase<RST, DT>*> &reduction_spaces)=0;

    void DeleteReductionSpaces();
    virtual void ResetAllReductionObjects(
        AReductionSpaceBase<RST, DT> &reduction_space,
        DT &val)=0;

    int NumProcessors();

    void InitReductionSpaces(int num_replicas, 
        AReductionSpaceBase<RST, DT> *conf_space);

  public:
    DISPEngineBase(
        // DISPCommBase<DT> *comm,
        AReductionSpaceBase<RST, DT> *conf_reduction_space, 
        int num_reduction_threads);
    virtual ~DISPEngineBase();

    virtual void RunParallelReduction(ADataRegion<DT> &input_data, int 
        req_units)=0;

    virtual void SeqInPlaceLocalSynchWrapper() = 0;
    virtual void ParInPlaceLocalSynchWrapper() = 0;
    virtual void DistInPlaceGlobalSynchWrapper() = 0;
    virtual void ResetReductionSpaces(DT &val) = 0;

    int num_procs() const {return num_procs_;};
    int num_reduction_threads() const {return num_reduction_threads_;};

    void Print();
};

template <typename RST, typename DT>
void DISPEngineBase<RST, DT>::InitReductionSpaces(int num_replicas, AReductionSpaceBase<RST, DT> *conf_space)
{
  reduction_spaces_.push_back(conf_space);
  for(int i=1; i<num_replicas; i++)
    reduction_spaces_.push_back(conf_space->Clone());
}

template <typename RST, typename DT>
void DISPEngineBase<RST, DT>::DeleteReductionSpaces()
{
  for(auto &obj : reduction_spaces_)
    delete obj;
}


template <typename RST, typename DT>
int DISPEngineBase<RST, DT>::NumProcessors(){
  int num_procs = 0;

  // With C++11 
  num_procs = std::thread::hardware_concurrency();
  // Below code should works with Linux, Solaris, AIX and Mac OS X(>= 10.4)
  //num_procs = sysconf(_SC_NPROCESSORS_ONLN);

  // If unable to compute hardware threads, set default to 1
  if(num_procs<1) num_procs = 1;
    // throw std::length_error("Number of available processors is <1!");

  return num_procs;
}

template <typename RST, typename DT>
DISPEngineBase<RST, DT>::DISPEngineBase(
    // DISPCommBase<DT> *comm,
    AReductionSpaceBase<RST, DT> *conf_reduction_space_i, 
    int num_reduction_threads):
  replication_type_(FULL_REPLICATION)
{
  num_procs_ = NumProcessors();
  num_reduction_threads_ = 
    (num_reduction_threads<1) ? num_procs_ : num_reduction_threads;

  // comm_ = comm;

  InitReductionSpaces(num_reduction_threads_, conf_reduction_space_i);
}

template <typename RST, typename DT>
DISPEngineBase<RST, DT>::~DISPEngineBase() {
  DeleteReductionSpaces();
}

template <typename RST, typename DT>
void DISPEngineBase<RST, DT>::Print(){
  DT total=0;
  std::cout << "DISPEngineBase::Print function is called.." << std::endl;
  for(auto &reduction_space : reduction_spaces_){
    std::cout << "reduction_space: " << 
      reduction_space->reduction_objects()[0][0] << std::endl;
    total+=reduction_space->reduction_objects()[0][0];
  }

  std::cout << "Total=" << total << std::endl;
}
#endif    // DISP_SRC_DISP_ENGINE_BASE_H_
