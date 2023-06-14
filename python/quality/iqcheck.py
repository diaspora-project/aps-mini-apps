from sewar.full_ref import msssim, mse, ssim, uqi, psnr
from skimage.exposure import adjust_gamma, rescale_intensity
import h5py as h5
import numpy as np

def rescale(reconstruction, hi):
  I = rescale_intensity(reconstruction, out_range=(0., 1.))
  return adjust_gamma(I, 1, hi)

fid1, fid2 = 0, 203
hi = 1
f1 = f"/home/beams2/TBICER/projects/Trace/build/bin/{fid1:06d}-recon.h5"
f2 = f"/home/beams2/TBICER/projects/Trace/build/bin/{fid2:06d}-recon.h5"

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

q = msssim(recon1i, recon0i)
print("MS-SSIM: {}".format(q))

q = ssim(recon1i, recon0i)
print("SSIM: {}".format(q))

q = uqi(recon1, recon0)
print("UQI: {}".format(q))

q = mse(recon1, recon0)
print("MSE: {}".format(q))

q = psnr(recon1i, recon0i)
print("PSNR: {}".format(q))
