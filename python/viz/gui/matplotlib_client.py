import matplotlib.pyplot as plt
import matplotlib.animation as manim
import numpy as np
import zmq
import math
import argparse
import sys

from local import flatbuffers
import TraceSerializer

from matplotlib.widgets import Slider, Button, RadioButtons


def update(val):
    fig.canvas.draw_idle()


builder = flatbuffers.Builder(0)
serializer = TraceSerializer.ImageSerializer(builder)


input_address = "tcp://localhost:50020"
recon_address = "tcp://localhost:52000"

plt.ion = True

ctx = zmq.Context()

sock = ctx.socket(zmq.SUB)
sock.setsockopt(zmq.SUBSCRIBE, b"")
sock.connect(input_address)

recon_sock = ctx.socket(zmq.SUB)
recon_sock.setsockopt(zmq.SUBSCRIBE, b"")
recon_sock.connect(recon_address)

poller = zmq.Poller()
poller.register(sock, zmq.POLLIN)
poller.register(recon_sock, zmq.POLLIN)

fig, (ax1, ax2) = plt.subplots(1, 2)
plt.pause(.01)

im1 = im2 = None
cnt = 0

axcolor = 'lightgoldenrodyellow'

rec_ax = plt.axes([0.15, 0.15, 0.65, 0.03], facecolor=axcolor)
rec_amp = Slider(rec_ax, 'Rec Scale', 0.1, 300.0, valinit=150)
rec_amp.on_changed(update)

proj_ax = plt.axes([0.15, 0.05, 0.65, 0.03], facecolor=axcolor)
proj_amp = Slider(proj_ax, 'Proj Scale', 0.1, 65000.0, valinit=30000)
proj_amp.on_changed(update)

while True:
  socks = dict(poller.poll(timeout=50))
  if sock in socks and socks[sock] == zmq.POLLIN:
    data = sock.recv()
    cnt += 1
    if cnt % 5 == 0:
      read_image = serializer.deserialize(serialized_image=data)
      proj = read_image.TdataAsNumpy()
      print("Projection={}; center={}".format(read_image.UniqueId(), read_image.Center()))
  
      proj.dtype = np.uint16
      proj = proj.reshape((read_image.Dims().Y(), read_image.Dims().X()))
      if im1 is None:
          im1 = ax1.imshow(proj, cmap='gray', vmin=0, vmax=45000)
      else:
          im1.set_data(proj)
          im1.set_clim(0, proj_amp.val)
      fig.canvas.draw_idle()
      plt.pause(0.01)
  if recon_sock in socks and socks[recon_sock] == zmq.POLLIN:
    print ("got recon data")
    recon = recon_sock.recv_pyobj()
    print(recon.shape)

    if im2 is None:
        im2 = ax2.imshow(recon[0], cmap='gray')
    else:
        im2.set_data(recon[0])
        im2.set_clim(0, rec_amp.val)
    fig.canvas.draw_idle()
    plt.pause(0.01)
  if not socks:
    plt.pause(.1)

