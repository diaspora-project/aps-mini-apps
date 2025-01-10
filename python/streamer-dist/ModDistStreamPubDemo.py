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
import csv

def parse_arguments():
  parser = argparse.ArgumentParser( description='Data Distributor Process')
  parser.add_argument('--protocol', default="na+sm", help='Mofka protocol')

  parser.add_argument('--group_file', type=str, default="mofka.json",
                      help='Group file for the mofka server')

  parser.add_argument('--batchsize', type=int, default=16,
                      help='mofka batch size')
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


  return parser.parse_args()

def main():
  args = parse_arguments()
  # Setup mofka
  mofka = MofkaDist(group_file=args.group_file, batchsize=args.batchsize)
  # Handshake with Sirt
  mofka.handshake(args.num_sinograms, args.num_columns)

  consumer = mofka.consumer(topic_name="daq_dist", consumer_name="dist")
  producer = mofka.producer(topic_name="dist_sirt", producer_name="producer_dist")
  mofka_t = []
  # Setup serializer
  serializer = TraceSerializer.ImageSerializer()

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

  while True:
    total_received += 1
    f = consumer.pull()
    event = f.wait()
    mofka_metadata = json.loads(event.metadata)
    if mofka_metadata["Type"] == "FIN": break
    total_received += 1
    mofka_data = event.data[0]
    total_size += len(mofka_data)

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
      print("before push image")
      t = mofka.push_image(mofka_sub,
                           args.num_sinograms,
                           ncols,
                           rotation,
                           mofka_read_image.UniqueId(),
                           mofka_read_image.Center(),
                           producer=producer)
      mofka_t.extend(t)
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

  time1 = time.time()

  # Profile information
  elapsed_time = time1-time0
  tot_MiBs = (total_size*1.)/2**20
  print("Received number of projections: {}; Total size (MiB): {:.2f}; Elapsed time (s): {:.2f}".format(total_received, tot_MiBs, elapsed_time))
  print("Rate (MiB/s): {:.2f}; (msg/s): {:.2f}".format(
            tot_MiBs/elapsed_time, total_received/elapsed_time))

  fields = ["push", "projection_id", "start", "stop", "duration", "size"]
  with open('Dist_push.csv', 'w') as f:
    write = csv.writer(f)
    write.writerow(fields)
    write.writerows(mofka_t)
  mofka.done_image(producer)
  del producer
  del consumer

if __name__ == '__main__':
  main()
