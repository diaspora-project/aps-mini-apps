import numpy as np
#import matplotlib.pyplot as plt
from skimage.exposure import adjust_gamma, rescale_intensity
from qcheck import *
import h5py as h5


def rescale(reconstruction, hi):
  I = rescale_intensity(reconstruction, out_range=(0., 1.))
  return adjust_gamma(I, 1, hi)

files = [ 'recon-shale-x8-i5.h5',
          'recon-shale-x8-i10.h5',
          'recon-shale-x8-i20.h5',
          'recon-shale-x8-i40.h5',
          'recon-shale-x8-i60.h5',
          'recon-shale-x8-i80.h5' ]

hi = 1
data_list = []
for irf in files:
  ir = h5.File(irf, 'r')
  recon = np.array(ir['data'])
  recon.shape
  ndata = np.reshape(recon, (recon.shape[1], recon.shape[2]))
  ndata = rescale(np.rot90(ndata)[::-1], hi)
  data_list.append(ndata)
  ir.close()

# Assume last image is ground truth
metrics = compute_quality(data_list[len(data_list)-1], data_list, method="MSSSIM")

for metric in metrics:
  print("------------")
  print(metric.qualities)
  print(metric.scales)

#plot_metrics(metrics)
#plt.show()
