"""fakapp-denoiser: protocol-equivalent stand-in for tekapp-denoiser.

Consumes from sirt_den exactly as tekapp does (nproc_sirt FIN markers to
terminate), sorts each batch by (iteration_stream, rank) like tekapp does,
but never invokes a model and never writes HDF5 output.
"""

import argparse
import json
import time

import numpy as np
import diaspora_stream.api as diaspora

from fakapp.common.ts_collector import TimestampCollector


def _run(model_path, driver_type, driver_config_file, batchsize, nproc_sirt):
  driver_options = {}
  if driver_config_file:
    with open(driver_config_file) as f:
      driver_options = json.load(f)
  elif driver_type == "files":
    driver_options = {"root_path": "./diaspora-data"}
  driver = diaspora.Driver(backend=driver_type, options=driver_options)

  topic = driver.open_topic("sirt_den")
  consumer = topic.consumer(name="denoiser", batch_size=batchsize)

  ts_collector = TimestampCollector()
  time0 = time.perf_counter()
  more_data = True
  cpt = nproc_sirt
  while more_data:
    data = []
    metadata = []
    for i in range(nproc_sirt * batchsize):
      ts_collector.record("PULL_START topic=sirt_den")
      f = consumer.pull()
      ts_collector.record("PULL_END topic=sirt_den")
      ts_collector.record("PULL_WAIT_START topic=sirt_den")
      event = None
      while event is None:
        event = f.wait(timeout_ms=300000)
      m = event.metadata
      m["diaspora_e_id"] = event.event_id
      m["diaspora_e_partition"] = event.partition
      if m["Type"] == "FIN":
        ts_collector.record(
            f"PULL_WAIT_END topic=sirt_den,event_id={event.event_id},data_size=0")
        cpt -= 1
        if cpt == 0:
          more_data = False
          break
      else:
        metadata.append(m)
        dd = bytearray(event.data[0])
        ts_collector.record(
            f"PULL_WAIT_END topic=sirt_den,event_id={event.event_id},"
            f"data_size={len(dd)}")
        dd = np.frombuffer(dd, dtype=np.float32)
        try:
          dd = dd.reshape(metadata[-1]["rank_dims"])
          data.append(dd)
        except Exception:
          print(metadata, dd.shape, dd, flush=True)

    if len(metadata) > 0:
      keyed = sorted(zip(metadata, data),
                     key=lambda md: (md[0]["iteration_stream"], md[0]["rank"]))
      correct_order_meta = [m for m, _ in keyed]
      correct_order = [d for _, d in keyed]

      for j in range(len(correct_order_meta) // nproc_sirt):
        batch_meta = correct_order_meta[j * nproc_sirt:(j + 1) * nproc_sirt]
        batch_data = correct_order[j * nproc_sirt:(j + 1) * nproc_sirt]
        print(batch_meta, flush=True)
        # tekapp concatenates + writes HDF5 here; fakapp drops the data.
        _ = np.concatenate(batch_data, axis=0) if batch_data else None

  print("Time to solution: ", time.perf_counter() - time0, flush=True)
  ts_collector.write("den.0.ts.txt")
  consumer.unsubscribe()
  del consumer
  del topic
  del driver


def main():
  parser = argparse.ArgumentParser(description='fakapp denoiser (no model, no HDF5).')
  parser.add_argument('--input', type=str, required=False,
                      help='Input file/directory (ignored).')
  parser.add_argument('--model', type=str, required=True,
                      help='Path to the saved model (ignored).')
  parser.add_argument("--batchsize", type=int, required=True, help="Mofka batchsize")
  parser.add_argument("--nproc_sirt", type=int, required=True,
                      help="Number of SIRT processes")
  parser.add_argument('--driver_type', type=str, default="files",
                      help='Type of Diaspora driver')
  parser.add_argument('--driver_config_file', type=str, default="",
                      help='JSON config file for Diaspora Driver')
  args = parser.parse_args()
  _run(args.model, args.driver_type, args.driver_config_file,
       args.batchsize, args.nproc_sirt)


if __name__ == "__main__":
  main()
