import numpy as np

class Trace():
## Hydrogen electron probability density
  def psi(i, j, k, offset=(50,50,100)):
    x = i-offset[0]
    y = j-offset[1]
    z = k-offset[2]
    th = np.arctan2(z, (x**2+y**2)**0.5)
    phi = np.arctan2(y, x)
    r = (x**2 + y**2 + z **2)**0.5
    a0 = 2
    ps = (1./81.) * 1./(6.*np.pi)**0.5 * (1./a0)**(3/2) * (r/a0)**2 * np.exp(-r/(3*a0)) * (3 * np.cos(th)**2 - 1)

    return ps

  @staticmethod
  def generate():
    data = np.fromfunction(Trace.psi, (100,100,200))
    positive = np.log(np.clip(data, 0, data.max())**2)
    negative = np.log(np.clip(-data, 0, -data.min())**2)

    d2 = np.empty(data.shape + (4,), dtype=np.ubyte)
    d2[..., 0] = positive * (255./positive.max())
    d2[..., 1] = negative * (255./negative.max())
    d2[..., 2] = d2[...,1]
    d2[..., 3] = d2[..., 0]*0.3 + d2[..., 1]*0.3
    d2[..., 3] = (d2[..., 3].astype(float) / 255.) **2 * 255

    d2[:, 0, 0] = [255,0,0,100]
    d2[0, :, 0] = [0,255,0,100]
    d2[0, 0, :] = [0,0,255,100]

    return d2

class DAQ():
  def __init__(self):
    self.ndata=0

  def generate(self, dims):
    self.ndata += 1
    return np.random.normal(size=(dims[0], dims[1]), loc=1024, scale=64).astype(np.uint16) #np.random.rand(dims[0], dims[1]).astype(np.uint16)
