"""fakapp-dist: protocol-equivalent stand-in for tekapp-dist.

Same handshake + dist_sirt traffic as tekapp-dist, but skips the
tomopy normalization / stripe-removal / log path. Every projection
received from daq_dist is replaced by a zero-filled float32 sinogram
of the same size that tekapp would have produced.
"""

import argparse
import time

import numpy as np

from fakapp.common import serializer as TraceSerializer
from fakapp.streamer_dist.diaspora_dist import DiasporaDist


def parse_arguments():
  parser = argparse.ArgumentParser(description='Data Distributor (fake)')
  parser.add_argument('--driver_type', type=str, default="files",
                      help='Type of Diaspora driver')
  parser.add_argument('--driver_config_file', type=str, default="",
                      help='JSON config file for Diaspora Driver')
  parser.add_argument('--batchsize', type=int, default=16,
                      help='Streaming batch size')
  parser.add_argument('--nproc_sirt', type=int, default=0,
                      help='Number of reconstruction processes')
  parser.add_argument('--beg_sinogram', type=int,
                      help='Starting sinogram (informational only)')
  parser.add_argument('--num_sinograms', type=int,
                      help='Number of sinograms to reconstruct (rows)')
  parser.add_argument('--num_columns', type=int,
                      help='Number of columns (cols)')

  # Compatibility flags — accepted, no-op (real math lives in tekapp-dist).
  parser.add_argument('--degree_to_radian', action='store_true', default=False)
  parser.add_argument('--mlog', action='store_true', default=False)
  parser.add_argument('--uint16_to_float32', action='store_true', default=False)
  parser.add_argument('--uint8_to_float32', action='store_true', default=False)
  parser.add_argument('--cast_to_float32', action='store_true', default=False)
  parser.add_argument('--normalize', action='store_true', default=False)
  parser.add_argument('--remove_invalids', action='store_true', default=False)
  parser.add_argument('--remove_stripes', action='store_true', default=False)
  parser.add_argument('--skip_serialize', action='store_true', default=False)
  parser.add_argument('--check_seq', action='store_true', default=False)
  return parser.parse_args()


def main():
  args = parse_arguments()

  diaspora_dist = DiasporaDist(driver_type=args.driver_type,
                               driver_config_file=args.driver_config_file,
                               batchsize=args.batchsize)
  diaspora_dist.handshake(args.nproc_sirt, args.num_sinograms, args.num_columns)

  consumer = diaspora_dist.consumer(topic_name="daq_dist",
                                    consumer_name="dist")
  producer = diaspora_dist.producer(topic_name="dist_sirt",
                                    producer_name="producer_dist")
  serializer = TraceSerializer.ImageSerializer()

  # Pre-allocate the zero-filled sinogram once: every projection forwarded
  # to SIRT has the same byte size, partitioned in generate_worker_msgs.
  fake_sub = np.zeros(args.num_sinograms * args.num_columns, dtype=np.float32)

  total_received = 0
  total_size = 0
  seq = 0
  time0 = time.time()
  while True:
    metadata, data = diaspora_dist.pull_image(consumer)
    if metadata["Type"] == "FIN":
      break
    total_received += 1
    total_size += len(data)
    if args.skip_serialize:
      print("Skipping rest. Received msg: {}".format(total_received))
      continue

    read_image = serializer.deserialize(serialized_image=data)
    serializer.info(read_image)

    if read_image.Itype() is serializer.ITypes.Projection:
      rotation = read_image.Rotation()
      diaspora_dist.push_image(fake_sub, args.num_sinograms,
                               args.num_columns, rotation,
                               read_image.UniqueId(), read_image.Center(),
                               producer=producer)
    # White/Dark/Reset frames are accumulated by tekapp for normalization;
    # fakapp doesn't normalize, so they are simply consumed and dropped.
    seq += 1

  diaspora_dist.last_flush(producer)
  elapsed = time.time() - time0
  tot_MiBs = total_size / 2 ** 20
  print("Received number of projections: {}; Total size (MiB): {:.2f}; "
        "Elapsed time (s): {:.2f}".format(total_received, tot_MiBs, elapsed))
  if elapsed > 0:
    print("Rate (MiB/s): {:.2f}; (msg/s): {:.2f}".format(
        tot_MiBs / elapsed, total_received / elapsed))

  diaspora_dist.done_image(producer)
  diaspora_dist.ts.write("dist.0.ts.txt")
  del producer
  consumer.unsubscribe()
  del consumer
  print("Exiting ...")


if __name__ == '__main__':
  main()
