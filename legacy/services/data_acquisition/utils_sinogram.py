import numpy as np
from logger_config import logger

def _convert_radian(self, itheta):
  m = np.max(itheta)
  if m > 2 * np.pi:
    logger.info(f"Theta values are in degree (max={m}); converting to radian.")
    itheta = itheta * np.pi / 180
  return itheta

def _ordered_subset(self, max_ind, nelem):
  nsubsets = np.floor(max_ind / nelem).astype(int)
  all_arr = np.array([])
  for i in np.arange(nsubsets):
    all_arr = np.append(all_arr, np.arange(start=i, stop=max_ind, step=nsubsets))
  return all_arr.astype(int)