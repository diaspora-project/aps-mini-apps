"""fakapp-daq: protocol-equivalent stand-in for tekapp-daq.

Emits identical Diaspora traffic (topic daq_dist, FlatBuffer TImage payloads,
{"index": N, "Type": "DATA"} metadata, terminating FIN) but synthesizes
np.zeros() projections instead of loading HDF5 via dxchange/tomopy.
"""

import argparse
import json
import signal
import time

import numpy as np
import diaspora_stream.api as diaspora

from fakapp.common import serializer as TraceSerializer
from fakapp.common.ts_collector import TimestampCollector


def parse_arguments():
  parser = argparse.ArgumentParser(description='Data Acquisition Process Simulator (fake)')
  parser.add_argument('--mode', type=int, required=True,
                      help='Data acquisition mode (0=detector; 1=simulate; 2=test)')
  parser.add_argument('--image_pv', help='EPICS image PV name (ignored).')
  parser.add_argument('--driver_type', type=str, default="files",
                      help='Type of Diaspora Driver to use')
  parser.add_argument('--driver_config_file', type=str, default="",
                      help='JSON config file for the Diaspora Driver')
  parser.add_argument('--batchsize', type=int, default=16,
                      help='Mofka batch size')
  parser.add_argument('--publisher_addr', default="tcp://*:50000",
                      help='Publisher address (ignored).')
  parser.add_argument('--publisher_hwm', type=int, default=0,
                      help='Publisher HWM (ignored).')
  parser.add_argument('--synch_addr', help='Synchronization address (ignored).')
  parser.add_argument('--synch_count', type=int, default=1,
                      help='Number of expected subscribers (ignored).')
  parser.add_argument('--simulation_file',
                      help='File name for mock data acquisition (ignored; fake data synthesized in memory).')
  parser.add_argument('--d_iteration', type=int, default=1,
                      help='Number of iterations on simulated data.')
  parser.add_argument('--iteration_sleep', type=float, default=0,
                      help='Delay between iterations.')
  parser.add_argument('--beg_sinogram', type=int, default=0,
                      help='Starting sinogram (informational only).')
  parser.add_argument('--num_sinograms', type=int, default=0,
                      help='Number of sinograms per projection.')
  parser.add_argument('--num_sinogram_columns', type=int,
                      help='Number of columns per sinogram.')
  parser.add_argument('--num_sinogram_projections', type=int,
                      help='Number of projections per sinogram.')
  return parser.parse_args()


bsignal = False


def _serialize_one(serializer, dims, uniqueId, rotation, seq):
  # Match tekapp test_daq dtype/shape (float32, (num_sinograms, num_cols)).
  image = np.zeros(dims, dtype='float32')
  return serializer.serialize(image=image, uniqueId=uniqueId,
                              itype=serializer.ITypes.Projection,
                              rotation=rotation, seq=seq)


def fake_daq(producer, num_sinograms, num_sinogram_columns,
             num_sinogram_projections, iterations, slp, ts):
  """Mirrors the per-projection cadence of tekapp's simulate_daq and test_daq.

  For each iteration, sends num_sinogram_projections fake projections to
  daq_dist with metadata {"index": int, "Type": "DATA"}, prints
  "Sending projection N" exactly like tekapp.
  """
  global bsignal
  if num_sinograms < 1:
    num_sinograms = 2048
  if num_sinogram_columns is None:
    num_sinogram_columns = 2048
  if num_sinogram_projections is None:
    num_sinogram_projections = 1440

  dims = (num_sinograms, num_sinogram_columns)
  serializer = TraceSerializer.ImageSerializer()

  tot_transfer_size = 0
  nproj = 0
  seq = 0
  time0 = time.time()
  for it in range(iterations):
    print("Current iteration over dataset: {}/{}".format(it + 1, iterations))
    for index in range(num_sinogram_projections):
      if bsignal:
        print("Signal received, exiting iteration loop.")
        bsignal = False
        break
      rotation = float(index) * 0.25  # match test_daq's default rotation_step
      serialized = _serialize_one(serializer, dims, index + 7, rotation, seq)
      md = {"index": int(index), "Type": "DATA"}
      print("Sending projection {}".format(index))
      ts.record(f"PUSH_START topic=daq_dist,data_size={len(serialized)}")
      producer.push(md, serialized)
      ts.record("PUSH_END topic=daq_dist")
      tot_transfer_size += len(serialized)
      seq += 1
      nproj += 1
    time.sleep(slp)

  ts.record("FLUSH_START topic=daq_dist")
  producer.flush().wait(timeout_ms=300000)
  ts.record("FLUSH_END topic=daq_dist")
  elapsed = time.time() - time0
  tot_MiBs = tot_transfer_size / 2 ** 20
  print("Sent number of projections: {}; Total size (MiB): {:.2f}; "
        "Elapsed time (s): {:.2f}".format(nproj, tot_MiBs, elapsed))
  if elapsed > 0:
    print("Rate (MiB/s): {:.2f}; (msg/s): {:.2f}".format(
        tot_MiBs / elapsed, nproj / elapsed))
  return seq


def main():
  args = parse_arguments()

  def signal_handler(sig, frame):
    global bsignal
    bsignal = True
  signal.signal(signal.SIGINT, signal_handler)

  driver_options = {}
  if args.driver_config_file:
    with open(args.driver_config_file) as f:
      driver_options = json.load(f)
  elif args.driver_type == "files":
    driver_options = {"root_path": "./diaspora-data"}
  driver = diaspora.Driver(backend=args.driver_type, options=driver_options)

  topic = driver.open_topic("daq_dist")
  producer = topic.producer("daq_producer",
                            batch_size=args.batchsize,
                            ordering=diaspora.Ordering.Strict)

  ts = TimestampCollector()
  time0 = time.time()

  if args.mode == 0:
    print("Mode 0 (detector) not supported in fakapp; treating as mode 2.")
    fake_daq(producer, args.num_sinograms, args.num_sinogram_columns,
             args.num_sinogram_projections, 1, args.iteration_sleep, ts)
  elif args.mode == 1:
    print("Simulating data acquisition (fake); file={} (ignored); iteration={}".format(
        args.simulation_file, args.d_iteration))
    fake_daq(producer, args.num_sinograms, args.num_sinogram_columns,
             args.num_sinogram_projections, args.d_iteration,
             args.iteration_sleep, ts)
  elif args.mode == 2:
    fake_daq(producer, args.num_sinograms, args.num_sinogram_columns,
             args.num_sinogram_projections, 1, args.iteration_sleep, ts)
  else:
    print("Unknown mode: {}".format(args.mode))

  ts.record("PUSH_START topic=daq_dist,data_size=1")
  producer.push({"Type": "FIN"}, bytearray(1))
  ts.record("PUSH_END topic=daq_dist")
  ts.record("FLUSH_START topic=daq_dist")
  producer.flush().wait(timeout_ms=300000)
  ts.record("FLUSH_END topic=daq_dist")
  ts.write("daq.0.ts.txt")

  print("Total time (s): {:.2f}".format(time.time() - time0))
  del producer
  del topic
  del driver
  print("Exiting ...")


if __name__ == '__main__':
  main()
