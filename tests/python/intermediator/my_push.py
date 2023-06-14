#!/usr/bin/python
# -*- coding: UTF-8 -*-

import sys
import zmq
import time
from threading import Thread


class Poller(Thread):

  def __init__(self, id, cmsg):
    super().__init__()
    self.id = id
    self.cmsg = cmsg

  def run(self):
    print('start poller {}'.format(self.id))
    worker = context.socket(zmq.PUSH)
    worker.connect("tcp://127.0.0.1:50000")
    self.loop = True
    i=0
    while self.loop:
      msg = "{}: i={}; cmsg={}".format(str(self.id), i, self.cmsg)
      print(msg)
      worker.send_string(msg)
      i = i+1
      #time.sleep(1)

  def stop(self):
    self.loop = False

context = zmq.Context()

poller1 = Poller(1, 'poller 1')
poller1.start()

poller2 = Poller(2, 'poller 2')
poller2.start()

poller3 = Poller(3, 'poller 3')
poller3.start()

poller4 = Poller(4, 'poller 4')
poller4.start()

time.sleep(60*5)

time.sleep(10)
poller1.stop()
poller3.stop()


poller2.stop()
poller4.stop()

sys.exit(0)
