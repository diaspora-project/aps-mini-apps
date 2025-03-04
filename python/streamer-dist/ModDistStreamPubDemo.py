import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '../common'))
sys.path.append(os.path.join(os.path.dirname(__file__), '../common/local'))
import argparse
import re
import numpy as np
import zmq
import time
import math
import TraceSerializer
import tomopy as tp
import tracemq as tmq
import csv


def parse_arguments():
  parser = argparse.ArgumentParser( description='Data Distributor Process')
  parser.add_argument('--data_source_addr', default=None,
      help='Remote publisher address for the data source.')
  parser.add_argument('--data_source_hwm', type=int, default=0, 
      help='Sets high water mark value for this subscriber. Default=0.')
  parser.add_argument('--data_source_synch_addr', default=None,
      help='Synchronizes this subscriber to provided publisher REP socket address (publisher should wait for subscriptions)')

  # Publisher for the others
  parser.add_argument('--my_publisher_addr', default=None,
      help='Publishes preprocessed data on given publisher address.')
  parser.add_argument('--my_publisher_freq', type=int, default=1,
              help='Publishes preprocessed data to with given frequency. E.g. if this equals 2, every other preprocessed projection is published. Default is 1.')

  # TQ communication
  parser.add_argument('--my_distributor_addr', default=None, help='IP address to bind tmq')
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


  return parser.parse_args()


def synchronize_subs(context, publisher_rep_address, num_workers):
  sync_socket = context.socket(zmq.REQ)
  sync_socket.connect(publisher_rep_address)

  for i in range(num_workers):
    print(f"Sending sync message to daq for worker {i}")
    sync_socket.send(b'') # Send synchronization signal
    print("Waiting rep sync message from daq..")
    sync_socket.recv() # Receive reply
    print("Received sync message from daq..")


def main():
  args = parse_arguments()

  context = zmq.Context(io_threads=8)

  # TQM setup
  if args.my_distributor_addr is not None:
    addr_split = re.split("://|:", args.my_distributor_addr)
    tmq.init_tmq()
    # Handshake w. remote processes
    print(addr_split)
    tmq.handshake(addr_split[1], int(addr_split[2]), args.num_sinograms, args.num_columns)
  else: print("No distributor..")

  # Subscriber setup
  print("Subscribing to: {}".format(args.data_source_addr))
  subscriber_socket = context.socket(zmq.SUB)
  subscriber_socket.set_hwm(args.data_source_hwm)
  subscriber_socket.connect(args.data_source_addr)
  subscriber_socket.setsockopt(zmq.SUBSCRIBE, b'')

  # Local publisher socket
  if args.my_publisher_addr is not None:
    publisher_socket = context.socket(zmq.PUB)
    publisher_socket.set_hwm(200) # TODO: parameterize high water mark value 
    publisher_socket.bind(args.my_publisher_addr)

  if args.data_source_synch_addr is not None:
    print(f"Synchronizing with {args.data_source_synch_addr} for {tmq.get_num_workers()}.")
    synchronize_subs(context, args.data_source_synch_addr, tmq.get_num_workers())
    print(f"Synchronized subscribers to daq for {tmq.get_num_workers()}.")

  zmq_producing_time = []
  zmq_consuming_time = []

  # Setup serializer
  serializer = TraceSerializer.ImageSerializer()

  # White/dark fields
  white_imgs=[]; tot_white_imgs=0; 
  dark_imgs=[]; tot_dark_imgs=0;
  
  # Receive images
  total_received=0
  total_size=0
  seq=0
  time0 = time.time()
  while True:
    msg = subscriber_socket.recv()
    total_received += 1
    total_size += len(msg)
    if msg == b"end_data": break # End of data acquisition

    # This is mostly for data rate tests
    if args.skip_serialize: 
      print("Skipping rest. Received msg: {}".format(total_received))
      continue 

    # Deserialize msg to image
    read_image = serializer.deserialize(serialized_image=msg)
    serializer.info(read_image) # print image information

    # Local checks
    if args.check_seq:
      if seq != read_image.Seq():
        print("Wrong sequence number: {} != {}".format(seq, read_image.Seq()))
      seq += 1
       

    # Push image to workers (REQ/REP)
    my_image_np = read_image.TdataAsNumpy()
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
    sub = sub.reshape((1, read_image.Dims().Y(), read_image.Dims().X()))



    # If incoming data is projection
    if read_image.Itype() is serializer.ITypes.Projection:
      rotation=read_image.Rotation()
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



      # Publish to the world
      if (args.my_publisher_addr is not None) and (total_received%args.my_publisher_freq==0):
        mub = np.reshape(sub,(read_image.Dims().Y(), read_image.Dims().X()))
        serialized_data = serializer.serialize(image=mub, uniqueId=0, rotation=0,
                    itype=serializer.ITypes.Projection)
        print("Publishing:{}".format(read_image.UniqueId()))
        publisher_socket.send(serialized_data)

      # Send to workers
      if args.num_sinograms is not None:
        sub = sub[:, args.beg_sinogram:args.beg_sinogram+args.num_sinograms, :]

      ncols = sub.shape[2] 
      sub = sub.reshape(sub.shape[0]*sub.shape[1]*sub.shape[2])

      if args.my_distributor_addr is not None:
        tmq.push_image(sub, args.num_sinograms, ncols, rotation, 
                        read_image.UniqueId(), read_image.Center())


    # If incoming data is white field
    if read_image.Itype() is serializer.ITypes.White: 
      #print("White field data is received: {}".format(read_image.UniqueId()))
      white_imgs.extend(sub)
      tot_white_imgs += 1

    # If incoming data is white-reset
    if read_image.Itype() is serializer.ITypes.WhiteReset: 
      #print("White-reset data is received: {}".format(read_image.UniqueId()))
      white_imgs=[]
      white_imgs.extend(sub)
      tot_white_imgs += 1

    # If incoming data is dark field
    if read_image.Itype() is serializer.ITypes.Dark:
      #print("Dark data is received: {}".format(read_image.UniqueId())) 
      dark_imgs.extend(sub)
      tot_dark_imgs += 1

    # If incoming data is dark-reset 
    if read_image.Itype() is serializer.ITypes.DarkReset:
      #print("Dark-reset data is received: {}".format(read_image.UniqueId())) 
      dark_imgs=[]
      dark_imgs.extend(sub)
      tot_dark_imgs += 1

  time1 = time.time()
    
  # Profile information
  elapsed_time = time1-time0
  tot_MiBs = (total_size*1.)/2**20
  print("Received number of projections: {}; Total size (MiB): {:.2f}; Elapsed time (s): {:.2f}".format(total_received, tot_MiBs, elapsed_time))
  print("Rate (MiB/s): {:.2f}; (msg/s): {:.2f}".format(
            tot_MiBs/elapsed_time, total_received/elapsed_time))

  # Finalize TMQ
  if args.my_distributor_addr is not None:
    print("Sending finalize message")
    tmq.done_image()
    tmq.finalize_tmq()



if __name__ == '__main__':
  main()
  
