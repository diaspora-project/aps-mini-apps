from sewar.full_ref import msssim, mse, ssim, uqi, psnr
from skimage.exposure import adjust_gamma, rescale_intensity
import h5py as h5
import numpy as np
import sys

def rescale(reconstruction, hi):
  I = rescale_intensity(reconstruction, out_range=(0., 1.))
  return adjust_gamma(I, 1, hi)

if __name__ == "__main__":
  if len(sys.argv) != 3:
    print("Usage: python iqcheck.py <fid1> <fid2>")
    sys.exit(1)
  
  f1 = sys.argv[1]
  f2 = sys.argv[2]

  hi = 1

  # read hdf5 file and return numpy array
  with h5.File(f1, 'r') as f:
    recon0 = np.array(f['data'])

  with h5.File(f2, 'r') as f:
    recon1 = np.array(f['data'])

  recon0 = rescale(np.rot90(recon0[0])[::-1], hi)
  recon1 = rescale(np.rot90(recon1[0])[::-1], hi)

  # readjust the range of the image to 0-255
  recon0i = (recon0 * 255).astype(np.uint8)
  recon1i = (recon1 * 255).astype(np.uint8)

  q0 = msssim(recon1i, recon0i)
  q1 = ssim(recon1i, recon0i)
  q2 = uqi(recon1, recon0)
  q3 = mse(recon1, recon0)
  q4 = psnr(recon1i, recon0i)

  print("MS-SSIM: {}; SSIM: {}; UQI: {}; MSE: {}; PSNR: {}".format(q0, q1, q2, q3, q4))