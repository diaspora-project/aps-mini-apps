import sys
import numpy as np
import time
import skimage
from skimage.io import imsave
import TDataModelBind


class XTraceImgReceive():
  def __init__(self, dataSourceAddr):
    self.dataSourceAddr = dataSourceAddr
    self.dataModel = TDataModelBind.DAQ()

    self.snapTime= 0.
    self.msgCount = 0


  def ConnectionInfo(self):
    print("Msg rate={}; # msgs={}; # lost msgs={}".format(
        (self.dataModel.ndata-self.msgCount)/(time.time()-self.snapTime),
        self.dataModel.ndata,
        self.dataModel.lostMsgs))
    self.snapTime = time.time()
    self.msgCount = self.dataModel.ndata


  def ReceiveImg(self):
    try: self.dataModel.connect(self.dataSourceAddr)
    except Exception as err:
      print(str(err))
      return
    print('Connected to {}'.format(self.dataSourceAddr))

    while True:
      img_data, itype = self.dataModel.receive()
      print("received img shape={}; counter={}; type={}".format(
              img_data.shape, self.dataModel.ndata, img_data.dtype))
      #for i in range(img_data.shape[0]):
      #  imsave("./tmp/{}_{}-img.jpg".format(self.dataModel.ndata, i), img_data[i,:,:])



if __name__ == "__main__":
    xt_img_receive = XTraceImgReceive("tcp://*:52000")
    xt_img_receive.ReceiveImg()

