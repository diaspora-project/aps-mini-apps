import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '../common'))
sys.path.append(os.path.join(os.path.dirname(__file__), '../common/local'))
import argparse
import numpy as np
import time
import math
import TraceSerializer
import tomopy as tp
import json
from mofka_dist import MofkaDist
from sst_dist import SSTDist
import csv
import signal
#from memory_profiler import profile
import threading
import multiprocessing

def parse_arguments():
  parser = argparse.ArgumentParser( description='Data Distributor Process')
  parser.add_argument('--protocol', default="na+sm", help='Mofka protocol')

  parser.add_argument('--dynamic_loadbalancing', default="false", help='Enable dynamic load balancing')

  parser.add_argument('--group_file', type=str, default="mofka.json",
                      help='Group file for the mofka server')

  parser.add_argument('--batchsize', type=int, default=16,
                      help='mofka batch size')

  parser.add_argument('--ntask_sirt', type=int, default=0,
                      help='number of reconstruction tasks')
  # TQ communication
  parser.add_argument('--beg_sinogram', type=int,
                          help='Starting sinogram for reconstruction')
  parser.add_argument('--num_sinograms', type=int,
                          help='Number of sinograms to reconstruct (rows)')
  parser.add_argument('--num_columns', type=int,
                          help='Number of columns (cols)')

  # Available pre-processing options
  parser.add_argument('--degree_to_radian', action='store_true', default=False,
              help='Converts rotation information to radian.')
  parser.add_argument('--mlog', action='store_true', default=False,
              help='Takes the minus log of projection data (projection data is divided by 50000 also).')
  parser.add_argument('--uint16_to_float32', action='store_true', default=False,
              help='Converts uint16 image byte sequence to float32.')
  parser.add_argument('--uint8_to_float32', action='store_true', default=False,
              help='Converts uint8 image byte sequence to float32.')
  parser.add_argument('--cast_to_float32', action='store_true', default=False,
              help='Casts incoming image byte sequence to float32.')
  parser.add_argument('--normalize', action='store_true', default=False,
              help='Normalizes incoming projection data with previously received dark and flat field.')
  parser.add_argument('--remove_invalids', action='store_true', default=False,
              help='Removes invalid measurements from incoming projections, i.e. negatives, nans and infs.')
  parser.add_argument('--remove_stripes', action='store_true', default=False,
              help='Removes stripes using fourier-wavelet method (in tomopy).')

  # Enable/disable steps
  parser.add_argument('--skip_serialize', action='store_true', default=False,
              help='Disable deserialization of the incoming messages. Only receives data from remote data source, since deserialization is required for other operations.')
  parser.add_argument('--check_seq', action='store_true', default=False,
              help='Checks the incoming packages sequence numbers and prints out when there is problem (does not terminate). Expected sequence is 0, 1, .., i, i+1, ...')

  parser.add_argument('--logdir', type=str, default='.',
              help='Path to save log files.')


  return parser.parse_args()


def flush_mofka_producer(p):
  while True:
    # time.sleep(0.1) # sleep to avoid busy-waiting
    time.sleep(0.01)
    print("Flushing mofka producer ...")
    p.flush()

# def task_to_worker_assignment(action_producer, action_consumer, args, action_mofka_dist):
def task_to_worker_assignment(args, num_workers):

  action_mofka_dist = MofkaDist(group_file=args.group_file, batchsize=args.batchsize)
  action_consumer = action_mofka_dist.consumer(topic_name="sirt_dist_action", consumer_name="dist")
  action_producer = action_mofka_dist.producer(topic_name="dist_sirt_action", producer_name="dist")

  print("Assigning tasks to workers ...")
  num_tasks = args.ntask_sirt
  # assign tasks to workers in round-robin fashion
  task_to_worker = {}
  worker_to_task = [[] for _ in range(num_workers)]
  for t in range(num_tasks):
    task_to_worker[t] = t % num_workers
    worker_to_task[t % num_workers].append(t)
  for w in range(num_workers):
    print(f"Worker {w} assigned tasks: {task_to_worker[w]}")
    for t in range(len(worker_to_task[w])):
      assign_info = {
          "Type": "START_TASK",
          "worker_id": w,
          "task_id": worker_to_task[w][t]
      }
      action_producer.push(assign_info, bytearray(1), partition=0)
      print(f"Send info to sirt: {assign_info}")
  action_producer.flush()

  task_progress = [0 for _ in range(num_tasks)]
  worker_progress = [0 for _ in range(num_workers)]

  total_progress = 0

  # Listen from consumer and take actions if needed
  while args.dynamic_loadbalancing.lower() == "true":
    f = action_consumer.pull()
    event = f.wait()
    metadata = json.loads(event.metadata)
    if metadata["Type"] == "PROGRESS":
      task_id = metadata["task_id"]
      worker_id = task_to_worker[task_id]
      progress = metadata["progress"]
      improved_progress = max(0, progress - task_progress[task_id])
      print(f"Received progress update: task {task_id} on worker {worker_id} progress {progress - improved_progress} --> {progress}")
      task_progress[task_id] = progress
      worker_progress[worker_id] += improved_progress
      total_progress += improved_progress
    else:
      print(f"Unknown metadata type received: {metadata['Type']}")
      continue
    
    # Make reassignment only if total progress is large enough to reflect performance
    if total_progress < num_tasks * 2:
      continue
      
    
    # sorting task_id based on progress from smallest to largest
    sorted_tasks = sorted(range(num_tasks), key=lambda x: task_progress[x])
    min_progress = sorted_tasks[0]
    max_progress = sorted_tasks[-1]
    if min_progress == max_progress:
      continue  # all tasks are at the same progress, no need to reassign
    task_lag = [(max_progress - task_progress[i])/(max_progress - min_progress) for i in range(num_tasks)]
    sum_lag = sum(task_lag)
    task_weights = [task_lag[i]*num_tasks/sum_lag for i in range(num_tasks)]
    
    sum_worker_progress = sum(worker_progress)
    worker_caps = [worker_progress[i]*num_tasks/sum_worker_progress for i in range(num_workers)]
    
    # reassign tasks based on weights and capacities
    worker_weights = [sum(task_weights[t] for t in worker_to_task[w]) for w in range(num_workers)]
    surplus_worker_caps = np.array(worker_caps) - np.array(worker_weights)
    sorted_surplus_worker_caps = sorted(range(num_workers), key=lambda x: surplus_worker_caps[x])

    task_assigned = False

    # Move task from worker with negative surplus to worker with positive surplus
    to_move_tasks = []
    for w in sorted_surplus_worker_caps:
      while surplus_worker_caps[w] < 0:
        # find task with largest weight to move
        tasks = worker_to_task[w]
        task_weights_in_worker = [task_weights[t] for t in tasks]
        max_weight_task = tasks[np.argmax(task_weights_in_worker)]
        to_move_tasks.append((max_weight_task, w))
      else:
        break
    
    # find workers with positive surplus to receive tasks
    for task in to_move_tasks:
      task_id, from_worker = task
      for w in sorted_surplus_worker_caps:
        if surplus_worker_caps[w] > task_weights[task_id]:
          # stop the max weight task on from_worker
          surplus_worker_caps[from_worker] += task_weights[task_id]
          worker_to_task[from_worker].remove(task_id)
          # notify sirt worker to stop this task
          stop_info = {
              "Type": "END_TASK",
              "worker_id": from_worker,
              "task_id": task_id
          }
          action_producer.push(stop_info, bytearray(1), partition=0)
          print(f"Stopping task {max_weight_task} on worker {w} for reassignment")
          # move task to this worker
          assign_info = {
              "Type": "START_TASK",
              "from_worker_id": from_worker,
              "to_worker_id": w,
              "task_id": task_id
          }
          action_producer.push(assign_info, bytearray(1), partition=0)
          print(f"Reassigning task {task_id} from worker {from_worker} to worker {w}")
          surplus_worker_caps[w] -= task_weights[task_id]
          worker_to_task[w].append(task_id)
          task_assigned = True
          action_producer.flush()
          break
      # reorder worker by surplus capacity after new assignments
      sorted_surplus_worker_caps = sorted(range(num_workers), key=lambda x: surplus_worker_caps[x])
    
    if task_assigned:
      # reset progress tracking after reassignment to make sure their performance is updated
      worker_progress = [0 for _ in range(num_workers)]
      total_progress = 0


#@profile
def main():
  args = parse_arguments()

  # Register a signal handler for this function
  def signal_handler(sig, frame):
    print("\nCtrl+C pressed. Exiting immediately...")
    sys.exit(0)  # Exit the program immediately

  signal.signal(signal.SIGINT, signal_handler)

  # Setup mofka
  print("Setup Mofka ...")
  mofka_dist = MofkaDist(group_file=args.group_file, batchsize=args.batchsize)

  # Handshake with Sirt
  print("Handshake with SIRT ...")
  mofka_dist.handshake(args.ntask_sirt, args.num_sinograms, args.num_columns)

  print("Setting up consumer and producer ...")
  consumer = mofka_dist.consumer(topic_name="daq_dist", consumer_name="dist")
  producer = mofka_dist.producer(topic_name="dist_sirt", producer_name="dist")

  # print("Setting up consumer and producer for actions ...")
  # # action_mofka_dist = MofkaDist(group_file=args.group_file, batchsize=args.batchsize)
  # action_mofka_dist = mofka_dist
  # action_consumer = action_mofka_dist.consumer(topic_name="sirt_dist_action", consumer_name="dist")
  # action_producer = action_mofka_dist.producer(topic_name="dist_sirt_action", producer_name="dist")

  # # Create a new thread to handle incoming actions from SIRT if needed,
  # # leaving the main thread to handle data distribution.
  # assignment_thread = threading.Thread(
  #   target=task_to_worker_assignment,
  #   args=(action_producer, action_consumer, args, action_mofka_dist),
  #   daemon=True
  # )
  # assignment_thread.start() 

  # Create a new process to run task to worker assignment
  num_workers = mofka_dist.nworkers
  assignment_process = multiprocessing.Process(
    target=task_to_worker_assignment_wrapper,
    args=(args, num_workers),
    daemon=True
  )
  assignment_process.start()

  print("Task assignment process started")

  mofka_producing_time = []
  mofka_consuming_time = []
  # Setup serializer
  serializer = TraceSerializer.ImageSerializer()

  print("Setting up serializer ...")

  # White/dark fields
  white_imgs=[]
  tot_white_imgs=0
  dark_imgs=[]
  tot_dark_imgs=0

  # Receive images
  total_received=0
  total_size=0
  seq=0
  time0 = time.time()

  sst_dist = SSTDist(num_sinograms=args.num_sinograms, chunk_size=args.num_columns, 
                             stream_name="sirt_stream", max_meta_bytes=65536)

  print("Starting to receive images ...")

  # Create a new thread to periodically flush the producer
  flush_thread = threading.Thread(target=flush_mofka_producer, args=(producer,), daemon=True)
  flush_thread.start()

  while True:

    mofka_metadata, mofka_data, pull_times = mofka_dist.pull_image(consumer)
    if mofka_metadata["Type"] == "FIN": break
    sequence_id = mofka_metadata["sequence_id"]
    total_received += 1
    total_size += len(mofka_data)
    mofka_consuming_time.append(pull_times)
    # This is mostly for data rate tests
    if args.skip_serialize:
      print("Skipping rest. Received msg: {}".format(total_received))
      continue

    # Deserialize msg to image
    mofka_read_image = serializer.deserialize(serialized_image=mofka_data)
    serializer.info(mofka_read_image) # print image information

    # # Local checks
    # if args.check_seq:
    #   if seq != mofka_read_image.Seq():
    #     print("Wrong sequence number: {} != {}".format(seq, mofka_read_image.Seq()))
    #   seq += 1

    # Push image to workers (REQ/REP)
    my_image_np = mofka_read_image.TdataAsNumpy()
    if args.uint8_to_float32:
      my_image_np.dtype = np.uint8
      sub = np.array(my_image_np, dtype="float32")
    elif args.uint16_to_float32:
      my_image_np.dtype = np.uint16
      sub = np.array(my_image_np, dtype="float32")
    elif args.cast_to_float32:
      my_image_np.dtype=np.float32
      sub = my_image_np
    else: sub = my_image_np

    sub = sub.reshape((1, mofka_read_image.Dims().Y(), mofka_read_image.Dims().X()))
    # If incoming data is projection
    if mofka_read_image.Itype() is serializer.ITypes.Projection:
      rotation=mofka_read_image.Rotation()
      if args.degree_to_radian: rotation = rotation*math.pi/180.

      # Tomopy operations expect 3D data, reshape incoming projections.
      if args.normalize:
        # flat/dark fields' corresponding rows
        if tot_white_imgs>0 and tot_dark_imgs>0:
          # print("normalizing: white_imgs.shape={}; dark_imgs.shape={}".format(
                  #np.array(white_imgs).shape, np.array(dark_imgs).shape))
          sub = tp.normalize(sub, flat=white_imgs, dark=dark_imgs)
      if args.remove_stripes:
        #print("removing stripes")
        sub = tp.remove_stripe_fw(sub, level=7, wname='sym16', sigma=1, pad=True)
      if args.mlog:
        #print("applying -log")
        sub = -np.log(sub)
      if args.remove_invalids:
        #print("removing invalids")
        sub = tp.remove_nan(sub, val=0.0)
        sub = tp.remove_neg(sub, val=0.00)
        sub[np.where(sub == np.inf)] = 0.00

      #to send from mofka:
      mofka_sub = sub.flatten()
      ncols = sub.shape[2]
      print(f"Sending image seq_id {sequence_id} to sirt through SST")
      tt = sst_dist.push_image(mofka_sub, sequence_id, args.num_sinograms, ncols, rotation,
                      mofka_read_image.UniqueId(), mofka_read_image.Center())
      print(f"Sending image seq_id {sequence_id} to sirt through Mofka")
      tt = mofka_dist.push_image(mofka_sub, sequence_id, args.num_sinograms, ncols, rotation,
                      mofka_read_image.UniqueId(), mofka_read_image.Center(), producer=producer)

      # if all(isinstance(item, list) for item in tt):
      #   mofka_producing_time.extend(tt)
      # else:
      #   mofka_producing_time.append(tt)

    # If incoming data is white field
    if mofka_read_image.Itype() is serializer.ITypes.White:
      #print("White field data is received: {}".format(read_image.UniqueId()))
      white_imgs.extend(sub)
      tot_white_imgs += 1

    # If incoming data is white-reset
    if mofka_read_image.Itype() is serializer.ITypes.WhiteReset:
      #print("White-reset data is received: {}".format(read_image.UniqueId()))
      white_imgs=[]
      white_imgs.extend(sub)
      tot_white_imgs += 1

    # If incoming data is dark field
    if mofka_read_image.Itype() is serializer.ITypes.Dark:
      #print("Dark data is received: {}".format(read_image.UniqueId()))
      dark_imgs.extend(sub)
      tot_dark_imgs += 1

    # If incoming data is dark-reset
    if mofka_read_image.Itype() is serializer.ITypes.DarkReset:
      #print("Dark-reset data is received: {}".format(read_image.UniqueId()))
      dark_imgs=[]
      dark_imgs.extend(sub)
      tot_dark_imgs += 1
    seq+=1

  t = mofka_dist.last_flush(producer)
  if t is not None:
    mofka_producing_time.append(t)
  time1 = time.time()

  # Profile information
  elapsed_time = time1-time0
  tot_MiBs = (total_size*1.)/2**20
  print("Received number of projections: {}; Total size (MiB): {:.2f}; Elapsed time (s): {:.2f}".format(total_received, tot_MiBs, elapsed_time))
  print("Rate (MiB/s): {:.2f}; (msg/s): {:.2f}".format(
            tot_MiBs/elapsed_time, total_received/elapsed_time))

  mofka_dist.done_image(producer)
  fields = ["type", "projection_id", "start", "stop", "duration", "metadata_size" ,"data_size"]
  with open(args.logdir + '/Dist_push.csv', 'w') as f:
    write = csv.writer(f)
    write.writerow(fields)
    write.writerows(mofka_producing_time)
  fields = ["t_wait", "t_metadata", "metadata_size" ,"t_data", "data_size"]
  with open(args.logdir + '/Dist_pull.csv', 'w') as f:
    write = csv.writer(f)
    write.writerow(fields)
    write.writerows(mofka_consuming_time)
  del producer
  del consumer
  
  # print("Notifying SIRT that we are done ...")
  # for w in range(mofka_dist.nworkers):
  #   action_info = {
  #       "Type": "SHUTDOWN",
  #       "worker_id": w
  #   }
  #   action_producer.push(action_info)
  #   action_producer.flush()
  
  # del action_producer
  # del action_consumer

  # Wait for the assignment process to finish
  assignment_process.join(timeout=5)
  if assignment_process.is_alive():
    assignment_process.terminate()

  # print("Complete data disitribution, sleeping until to exit ...")
  # while True:
  #   time.sleep(1)

  sst_dist.close()
    

  print("Exiting ...")

def task_to_worker_assignment_wrapper(args, num_workers):
  """Wrapper to ensure output is flushed to the parent process"""
  sys.stdout.flush()
  sys.stderr.flush()
  task_to_worker_assignment(args, num_workers)
  sys.stdout.flush()
  sys.stderr.flush()

if __name__ == '__main__':
  main()
