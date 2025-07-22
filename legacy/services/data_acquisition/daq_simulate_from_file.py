import sys
import time
import signal
import numpy as np
from logger_config import logger

class DAQSimulator:
  def __init__(self):
    self.bsignal = False
    signal.signal(signal.SIGINT, self._signal_handler)
  

  def _signal_handler(self, sig, frame):
    self.bsignal = True
    logger.info(f"Signal handler called with signal {sig}")

  def _read_from_file(self, input_f, beg_sinogram=0, num_sinograms=0, save_after_serialize=False):
    serialized_data = None
    if input_f.endswith('.npy'):
      serialized_data = np.load(input_f, allow_pickle=True)
    else:
      idata, flat, dark, itheta = self._setup_simulation_data(input_f, beg_sinogram, num_sinograms)
      serialized_data = self._serialize_dataset(idata, flat, dark, itheta)
      if save_after_serialize:
        np.save("{}.npy".format(input_f), serialized_data)
      del idata, flat, dark
    return serialized_data

  def _process_signal(self):
    while True:
      cmd = input("\nSignal received, (q)uit or (c)ontinue: ")
      if cmd in ('q', 'c'):
        break
      else:
        logger.error(f"Invalid command received: {cmd}. Please enter 'q' to quit or 'c' to continue.")
    if cmd == 'q':
      logger.info("Exiting...")
      raise SystemExit("Exiting due to signal interruption.")
    if cmd == 'c':
      self.bsignal = False
      logger.info("Continue streaming projections.")


  def simulate_from_file(self, publisher_socket, input_f,
               beg_sinogram=0, num_sinograms=0, seq=0, slp=0,
               iteration=1, save_after_serialize=False, prj_slp=0, 
               nelems_per_subset=16):

    serialized_data = self._read_from_file(input_f, 
                                           beg_sinogram, num_sinograms, 
                                           save_after_serialize)
    tot_transfer_size = 0
    time0 = time.time()
    indices = self._ordered_subset(serialized_data.shape[0],
                                   nelems_per_subset)
    for it in range(iteration):
      logger.info(f"Current iteration over dataset: {it + 1}/{iteration}")
      for index in indices:
        if self.bsignal: self._process_signal()
        logger.info("Sending projection {}".format(index))
        time.sleep(prj_slp)
        dchunk = serialized_data[index]
        publisher_socket.send(dchunk, copy=False)
        tot_transfer_size += len(dchunk)
      time.sleep(slp)
    time1 = time.time()

    elapsed_time = time1 - time0
    tot_MiBs = (tot_transfer_size * 1.) / 2 ** 20
    nproj = iteration * len(serialized_data)
    logger.info("Sent number of projections: {}; Total size (MiB): {:.2f}; Elapsed time (s): {:.2f}".format(nproj, tot_MiBs, elapsed_time))
    logger.info("Rate (MiB/s): {:.2f}; (msg/s): {:.2f}".format(tot_MiBs / elapsed_time, nproj / elapsed_time))

    return seq