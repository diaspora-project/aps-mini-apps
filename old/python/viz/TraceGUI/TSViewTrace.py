from PyQt5 import QtCore, QtGui, QtWidgets
from PyQt5.QtCore import QObject, pyqtSlot
from PyQt5.QtWidgets import QWidget
from PyQt5.uic import loadUi
import pyqtgraph.opengl as gl
import pyqtgraph as pg

import numpy as np
import TDataModel
import time
import skimage
import skimage.transform


class TSViewTrace(QWidget):
  def __init__(self):
    super(TSViewTrace, self).__init__()
    loadUi('TSViewTrace.ui', self)
    self.setWindowTitle('Trace Streaming Tomography Data Viewer')

    self.dataSourceAddr = None
    self.dataModel = TDataModel.DAQ()


    ## Add grid to the 3D volume
    g = gl.GLGridItem()
    g.scale(10, 10, 1)
    self.graphicsViewStreamingData.addItem(g)

    self.qtimer = QtCore.QTimer()
    self.qtimer.timeout.connect(self.updateImage)
    self.qtimerOn = False
    self.snapTime= 0.
    self.msgCount = 0

    # Register your events below

    # buttonConnectPublisher
    self.buttonConnectPublisher.clicked.connect(self.buttonConnectPublisher_clicked)

    # buttonTestView
    self.buttonTestView.clicked.connect(self.buttonTestView_clicked)


    self.textBoxPublisherAddress.setText('tcp://127.0.0.1:52000')



  # buttonConnectPublisher events
  @pyqtSlot()
  def buttonConnectPublisher_clicked(self):
    if self.qtimerOn is False: # There is no connection
      self.dataSourceAddr = self.textBoxPublisherAddress.text()
      self.labelStatusInfo.setText('Connecting {}'.format(self.dataSourceAddr))
      QtWidgets.QApplication.processEvents()

      try: self.dataModel.connect(self.dataSourceAddr, 100)
      except Exception as err: 
        self.labelStatusInfo.setText(str(err))
        return
      self.labelStatusInfo.setText('Connected to {}'.format(self.dataSourceAddr))
      self.buttonConnectPublisher.setText("Profile")

    # Initiate acquisition
    if self.qtimerOn is True:
      self.labelStatusInfo.setText('Current data rate: fps={}; # msgs={}; # lost msgs={}'.
                    format(
                      (self.dataModel.ndata-self.msgCount) /
                        (time.time()-self.snapTime), 
                      self.dataModel.ndata,
                      self.dataModel.lostMsgs))
      self.msgCount = self.dataModel.ndata
      self.snapTime = time.time()
    else:
      img, itype = self.dataModel.receive()

      self.qtimer.start(0)
      self.qtimerOn = True
      self.snapTime= time.time()
      self.labelStatusInfo.setText('Started receiving data')


  def updateImage(self):
    img_data, itype = self.dataModel.receive()
    print("received img shape={}".format(img_data.shape))
    # consider single img slice
    #img_data = skimage.transform.resize(img_data[0,:,:], (256,256), anti_aliasing=True)
    #img_data.shape = (1, img_data.shape[0], img_data.shape[1])
    #print(img_data.shape)
    img = np.empty([img_data.shape[0], 2048, 2048, 4], dtype=np.byte)
    img_max = np.amax(img_data)
    img_min = np.amin(img_data)
    img_data = np.array((img_data-img_min)/(img_max-img_min)*255, dtype=np.byte)
    img[:, :, :, 0] = img_data
    img[:, :, :, 1] = img_data
    img[:, :, :, 2] = img_data
    img[:, :, :, 3] = 255
    #img = skimage.color.gray2rgb(img_data)

    #img = pg.makeARGB(img_data[0 , :, :], levels=[img_min, img_max])
    print(img.shape)
    v = gl.GLVolumeItem(img)
    self.graphicsViewStreamingData.addItem(v)


  # buttonTestView events
  @pyqtSlot()
  def buttonTestView_clicked(self):
    ## Add grid to the 3D volume
    #g = gl.GLGridItem()
    #g.scale(10, 10, 1)
    #self.graphicsViewStreamingData.addItem(g)

    ## Add data to the 3D volume
    data = MockVizData.Trace.generate()
    v = gl.GLVolumeItem(data)
    v.translate(-50,-50,-100)
    self.graphicsViewStreamingData.addItem(v)



if __name__ == "__main__":
    import sys
    app = QtWidgets.QApplication(sys.argv)
    tsView = TSViewTrace()
    tsView.show()
    sys.exit(app.exec_())

  
