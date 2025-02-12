import matplotlib.pyplot as plt
import numpy as np
import zmq
import math
import argparse
import sys

from local import flatbuffers
import TraceSerializer

builder = flatbuffers.Builder(0)
serializer = TraceSerializer.ImageSerializer(builder)


input_address = "tcp://164.54.113.96:5560"
recon_address = "tcp://*:9999"

plt.ion = True

ctx = zmq.Context()

sock = ctx.socket(zmq.SUB)
sock.setsockopt(zmq.SUBSCRIBE, b"")
sock.connect(input_address)

recon_sock = ctx.socket(zmq.SUB)
recon_sock.setsockopt(zmq.SUBSCRIBE, b"")
recon_sock.bind(recon_address)

poller = zmq.Poller()
poller.register(sock, zmq.POLLIN)
poller.register(recon_sock, zmq.POLLIN)

f, (ax1, ax2) = plt.subplots(1, 2)
ax1.set_title('Simple View')

while True:
  socks = dict(poller.poll())
  if sock in socks and socks[sock] == zmq.POLLIN:
    print("got projection data")
    data = sock.recv()
    read_image = serializer.deserialize(serialized_image=data)
    my_image = read_image.TdataAsNumpy()

    my_image.dtype = np.uint16
    my_image = my_image.reshape((read_image.Dims().Y(), read_image.Dims().X()))
    print(my_image.shape, my_image.dtype)
    print("showing")
    print(my_image)
    ax1.imshow(my_image, cmap='viridis')
    plt.pause(0.01)
  if recon_sock in socks and socks[recon_sock] == zmq.POLLIN:
    print ("got recon data")
    image = recon_sock.recv_pyobj()
    print(image.shape)

    if image.shape[0] == 1:
      ax2.imshow(image[0], cmap='viridis')
    else:
      ax2.imshow(image, cmap='viridis')
    plt.pause(0.01)
