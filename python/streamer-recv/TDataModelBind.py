import sys
import os
import numpy as np
import zmq
sys.path.append(os.path.join(os.path.dirname(__file__), '../common'))
sys.path.append(os.path.join(os.path.dirname(__file__), '../common/local'))
import flatbuffers
import TraceSerializer


class Trace():
  pass

class DAQ():
  def __init__(self):
    # setup ZMQ socket
    self.subscriber_socket = None 
    # Setup flatbuffer serializer
    self.serializer = TraceSerializer.ImageSerializer()
    self.ndata = 0
    self.firstMsgId = 0
    self.lastMsgId = 0
    self.lostMsgs = 0
    self.zmqContext = zmq.Context()

  def connect(self, data_source_addr, data_source_hwm=0, timeout=100000):
    if self.subscriber_socket is None:
      self.subscriber_socket = self.zmqContext.socket(zmq.SUB)
    self.subscriber_socket.set_hwm(data_source_hwm) # TODO
    try: self.subscriber_socket.bind(data_source_addr)
    except zmq.error.ZMQError as err: 
      raise ValueError("ZMQ connection error -- {}".format(err))
    except Exception as err: 
      raise Exception("Unknown ZMQ error -- {}".format(err))
    self.subscriber_socket.setsockopt(zmq.SUBSCRIBE, b'')
    #self.subscriber_socket.setsockopt(zmq.RCVTIMEO, timeout) #TODO Add functionality for handling timeouts
    

  def receive(self):
    self.ndata += 1

    # XXX Exception needs to be handled:
    # zmq.error.Again: Resource temporarily unavailable
    try:
      msg = self.subscriber_socket.recv()
      if msg == b"end_data": return None # End of data acquisition
    except Exception as e:
      print("Exception handled:\n{}".format(e))

    img_msg = self.serializer.deserialize(serialized_image=msg)
    self.serializer.info(img_msg)

    currId = img_msg.Seq()
    if self.ndata == 1: 
      self.firstMsgId = currId
      self.lastMsgId = currId
    else: 
      diff = currId - self.lastMsgId - 1
      if diff > 0: self.lostMsgs += diff
      self.lastMsgId = currId 

    ddims = (img_msg.Dims().Z(), img_msg.Dims().Y(), img_msg.Dims().X())

    img = img_msg.TdataAsNumpy()
    bsize = int(img.shape[0]/(ddims[0]*ddims[1]*ddims[2]))

    if bsize == 4: img.dtype=np.float32
    elif bsize == 2: img.dtype=np.uint16
    elif bsize == 1: img.dtype=np.uint8
    else : raise Exception("Unknown image data type size: {}!".format(bsize))

    img = img.reshape(ddims)
    return img, img_msg.Itype()
