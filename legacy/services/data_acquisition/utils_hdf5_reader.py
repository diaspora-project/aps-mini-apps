import h5py as h5
import os
import sys
from logger_config import logger
import time
import numpy as np


def _read_hdf5_file(self, input_f):
  if not os.path.exists(input_f):
    logger.error(f"File not found: {input_f}")
    sys.exit(1)

  logger.info(f"Loading tomography data: {input_f}")
  t0 = time.time()
  with h5.File(input_f, 'r') as f:
    idata = f['exchange/data'][()]
    flat = f['exchange/data_white'][()]
    dark = f['exchange/data_dark'][()]
    itheta = f['exchange/theta'][()]
  logger.info("Projection dataset IO time={:.2f}; dataset shape={}; size={}; Theta shape={};".format(
    time.time() - t0, idata.shape, idata.size, itheta.shape))
  return idata, flat, dark, itheta


def _setup_simulation_data(self, input_f, beg_sinogram=0, num_sinograms=0, convert_to_radians=False):
  idata, flat, dark, itheta = self._read_hdf5_file(input_f)

  idata = np.array(idata, dtype=np.float32)
  if flat is not None:
    flat = np.array(flat, dtype=np.float32)
  if dark is not None:
    dark = np.array(dark, dtype=np.float32)
  if itheta is not None:
    itheta = np.array(itheta, dtype=np.float32)

  # check if all the itheta values are within radian range
  if convert_to_radians and itheta is not None:
    logger.info("Converting theta values to radians.")
    itheta = np.radians(itheta)

  return idata, flat, dark, itheta