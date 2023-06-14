import logging

import numpy as np
from qtpy.QtCore import *
from qtpy.QtGui import *
from qtpy.QtWidgets import *

from xicam.core import msg
from xicam.gui import threads
import pyqtgraph as pg
import zmq

from xicam.plugins import GUIPlugin, GUILayout

print("IN STREAMING")


class StreamingTomographyPlugin(GUIPlugin):
    name = 'StreamingTomography'

    def __init__(self):
        print("Initializing")
        self.logwidget = QListWidget()
        self.leftdisplay = pg.ImageView()
        self.rightdisplay = pg.ImageView()
        self.stages = {'StreamingTomography': GUILayout(self.leftdisplay, right=self.rightdisplay)}
        super(StreamingTomographyPlugin, self).__init__()

        self.thread = threads.QThreadFutureIterator(self.background_sock, callback_slot=self.display, showBusy=True)
        self.thread.start()

    @staticmethod
    def background_sock():
        print("IN HERE??")
        from .local import flatbuffers
        from . import TraceSerializer

        ctx = zmq.Context()
        builder = flatbuffers.Builder(0)
        serializer = TraceSerializer.ImageSerializer(builder)

        input_address = "tcp://164.54.113.96:5560"
        recon_address = "tcp://*:9999"

        sock = ctx.socket(zmq.SUB)
        sock.setsockopt(zmq.SUBSCRIBE, b"")
        sock.connect(input_address)

        recon_sock = ctx.socket(zmq.SUB)
        recon_sock.setsockopt(zmq.SUBSCRIBE, b"")
        recon_sock.bind(recon_address)

        poller = zmq.Poller()
        poller.register(sock, zmq.POLLIN)
        poller.register(recon_sock, zmq.POLLIN)

        while True:
          socks = dict(poller.poll())
          if sock in socks and socks[sock] == zmq.POLLIN:
              print("got projection data")
              data = sock.recv()
              read_image = serializer.deserialize(serialized_image=data)
              my_image = read_image.TdataAsNumpy()
              my_image.dtype = np.uint16
              my_image = my_image.reshape((read_image.Dims().Y(), read_image.Dims().X()))
              yield None, my_image

          if recon_sock in socks and socks[recon_sock] == zmq.POLLIN:
              print ("got recon data")
              image = recon_sock.recv_pyobj()
              print(image.shape)
              yield image[0], None 
    def display(self, left: np.ndarray, right: np.ndarray):
        if left is not None:
          self.leftdisplay.setImage(left)
        if right is not None:
          self.rightdisplay.setImage(right)
