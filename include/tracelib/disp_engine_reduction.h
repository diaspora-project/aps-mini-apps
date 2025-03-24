#ifndef DISP_SRC_DISP_ENGINE_REDUCTION_H_
#define DISP_SRC_DISP_ENGINE_REDUCTION_H_

#include "disp_engine_base.h"
#include "mirrored_region_bare_base.h"
#include "reduction_space_a.h"
#include <deque>

template <typename RST, typename DT>
class DISPEngineReduction : public DISPEngineBase<RST, DT>{
  protected:
    std::mutex work_queue_mutex;

    virtual MirroredRegionBareBase<DT>* Partitioner(ADataRegion<DT> &input_data,
        int req_units)
    {
      return input_data.NextMirroredRegion(req_units);
    }

    virtual MirroredRegionBareBase<DT>* PartitionWrapper(
        ADataRegion<DT> &input_data, int req_units)
    {
      std::lock_guard<std::mutex> lock(this->partitioner_mutex_);
      return Partitioner(input_data, req_units);
    }

    /**
     * \brief Wrapper for reduction thread.
     *
     * \param[in] reduction_space This thread's reduction space.
     * \param[in] input_data The whole input data.
     * \param[in] req_units The number of items that will be assigned after each
     * request. this thread will have from
     */
    virtual void ReductionWrapper(
        AReductionSpaceBase<RST, DT> &reduction_space,
        ADataRegion<DT> &input_data,
        int &req_units)
    {
      auto output_data = PartitionWrapper(input_data, req_units);
      while(output_data != nullptr){
        reduction_space.Process(*output_data);
        output_data = PartitionWrapper(input_data, req_units);
      }
    }

    virtual void SeqInPlaceLocalSynch(
        std::vector<AReductionSpaceBase<RST, DT> *> &reduction_spaces)
    {
      auto &head_reduction_space = *reduction_spaces[0];

      for(size_t i=1; i<reduction_spaces.size(); i++){
        RST *r = static_cast<RST *> (reduction_spaces[i]);
        head_reduction_space.LocalSynchWith(*r);
      }
    }

    virtual void ResetAllReductionObjects(
        AReductionSpaceBase<RST, DT> &reduction_space,
        DT &val)
    {
      reduction_space.reduction_objects().ResetAllItems(val);
      reduction_space.reduction_objects().ResetAllMirroredRegions();
    }


  public:
    // virtual void GlobalInPlaceSynch(
    //     DataRegion2DBareBase<DT> &dr,
    //     DISPCommBase<DT> &comm)
    // {
    //   comm.GlobalInPlaceCombination(dr);
    // };

    virtual void DistInPlaceGlobalSynchWrapper(){
     AReductionSpaceBase<RST, DT> &head_rs = *(this->reduction_spaces_)[0];
     DataRegion2DBareBase<DT> &dr = head_rs.reduction_objects();
    //  GlobalInPlaceSynch(dr, *(this->comm_));
    };

    virtual void SeqInPlaceLocalSynchWrapper(){
      SeqInPlaceLocalSynch(this->reduction_spaces_);
    };

    void ParInPlaceLocalSynchHelper(
        std::deque<std::vector<AReductionSpaceBase<RST, DT> *>*> &work_queue)
    {
      while(1){
        work_queue_mutex.lock();
        if(work_queue.size()>0){
          auto v_i = std::move(work_queue.front());
          work_queue.pop_front();
          work_queue_mutex.unlock();
          SeqInPlaceLocalSynch(*v_i);
          delete v_i;
        }
        else{
          work_queue_mutex.unlock();
          break;
        }
      }
    };

    void PartitionReductionSpaces(
        std::vector<AReductionSpaceBase<RST, DT> *> &input_spaces,
        int partition_size,
        std::deque<std::vector<AReductionSpaceBase<RST, DT> *>*> &work_queue,
        std::vector<AReductionSpaceBase<RST, DT> *> &target_spaces)
    {
      int j_iter = input_spaces.size() / partition_size;
      int j_remain = input_spaces.size() % partition_size;

      int i;
      for(i=0; i<j_iter; i++){
        int first = i*partition_size;
        auto begin = input_spaces.begin() + first;
        auto end = input_spaces.begin() + first + partition_size;
        auto v = new std::vector<AReductionSpaceBase<RST, DT> *>(begin, end);
        work_queue.push_back(v);
        target_spaces.push_back((*v)[0]);
      }
      if(j_remain>0){
        int first = i*partition_size;
        auto begin = input_spaces.begin() + first;
        auto end = input_spaces.begin() + first + j_remain;
        auto v = new std::vector<AReductionSpaceBase<RST, DT> *>(begin, end);
        work_queue.push_back(v);
        target_spaces.push_back((*v)[0]);
      }
    };

    virtual void ParInPlaceLocalSynch(
        std::vector<AReductionSpaceBase<RST, DT> *> &reduction_spaces,
        int partition_size,
        int num_threads)
    {
      std::deque<std::vector<AReductionSpaceBase<RST, DT> *>*> work_queue;
      std::vector<AReductionSpaceBase<RST, DT> *> target_spaces = reduction_spaces;
      std::vector<AReductionSpaceBase<RST, DT> *> temp_target_spaces;

      do{
        PartitionReductionSpaces(
            target_spaces,
            partition_size,
            work_queue,
            temp_target_spaces);

        std::vector<std::thread> worker_threads;
        for(int i=0; i<num_threads; i++){
          worker_threads.push_back(
            std::thread(
              &DISPEngineReduction::ParInPlaceLocalSynchHelper,
              this,
              std::ref(work_queue)));
        }

        for(auto &worker_thread : worker_threads)
          worker_thread.join();

        target_spaces = std::move(temp_target_spaces);
        temp_target_spaces.clear();
      }while(target_spaces.size()>1);
    }

    virtual void ParInPlaceLocalSynchWrapper(){
      ParInPlaceLocalSynch(this->reduction_spaces_, 2, this->num_reduction_threads_);
    }

    virtual void RunParallelReduction(ADataRegion<DT> &input_data, int req_units)
    {
      // Create threads
      std::vector<std::thread> reduction_threads;
      for(int i=0; i<this->num_reduction_threads_; i++){
        reduction_threads.push_back(std::thread(
              &DISPEngineReduction::ReductionWrapper,
              this,
              std::ref(*(this->reduction_spaces_)[i]),
              std::ref(input_data),
              std::ref(req_units)));
      }

      // Join threads
      for(auto &reduction_thread : reduction_threads)
        reduction_thread.join();
    }


    virtual void ResetReductionSpaces(DT &val){
      // Create threads
      std::vector<std::thread> reduction_threads;
      for(int i=0; i<this->num_reduction_threads_; i++){
        reduction_threads.push_back(std::thread(
              &DISPEngineReduction::ResetAllReductionObjects,
              this,
              std::ref(*(this->reduction_spaces_)[i]),
              std::ref(val)));
      }

      // Join threads
      for(auto &reduction_thread : reduction_threads)
        reduction_thread.join();
    }

    DISPEngineReduction(
        // DISPCommBase<DT> *comm,
        AReductionSpaceBase<RST, DT> *conf_reduction_space_i,
        int num_reduction_threads) :
      DISPEngineBase<RST, DT>(
          // comm,
          conf_reduction_space_i,
          num_reduction_threads){};
};

#endif    // DISP_SRC_DISP_ENGINE_REDUCTION_H_
