import time
from PyQt5 import QtCore, QtGui, QtWidgets
from PyQt5.QtCore import QObject, pyqtSlot
from PyQt5.QtWidgets import QWidget
from PyQt5.uic import loadUi
import pyqtgraph as pg
import tomopy as tp


import numpy as np
import TDataModel
import time


class TSViewDAQ(QWidget):
  def __init__(self):
    super(TSViewDAQ, self).__init__()
    loadUi('TSViewDAQ.ui', self)
    self.setWindowTitle('Trace Streaming Tomography 2D Viewer')

    self.dataSourceAddr = None
    self.dataModel = TDataModel.DAQ()

    self.ddims = (1024, 1024) # default to 1Kx1K
    self.viewBox = self.graphicsViewStreamingData.getView()
    self.viewBox.setAspectLocked(True)

    self.img = self.graphicsViewStreamingData.getImageItem()
    self.img.setOpts(axisOrder='row-major')

    self.img.setImage(np.random.randint(255, size=self.ddims, dtype=np.uint16))
    self.viewBox.autoRange()

    self.graphicsViewStreamingData.ui.menuBtn.setVisible(False)

    self.qtimer = QtCore.QTimer()
    self.qtimer.timeout.connect(self.updateImage)
    self.qtimerOn = False
    self.snapTime= 0.
    self.msgCount = 0

    # Register your view events
    # buttonConnectPublisher
    self.buttonConnectPublisher.clicked.connect(self.buttonConnectPublisher_clicked)


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
      img = tp.downsample(img, 2)

      # Fix the GUI area if the incoming image size doesn't fit
      # The first dimension is z, which is redundant for 2D view below
      if img.shape[1:] is not self.ddims: 
        print("Shape is not correct: {} vs {}".format(img.shape, self.ddims))
        self.ddims = img.shape[1:]
        self.viewBox.setRange(QtCore.QRectF(0, 0, self.ddims[0], self.ddims[1]))
        img, itype = self.dataModel.receive()
        self.graphicsViewStreamingData.setImage(img)
        #self.graphicsViewStreamingData.setImage(img[::-1,:])
        #self.img.setImage(img[::-1,:])
      self.qtimer.start(0)
      self.qtimerOn = True
      self.snapTime= time.time()
      self.labelStatusInfo.setText('Started receiving data')


  def updateImage(self):
    #self.img.setImage(np.flipud(self.dataModel.receive()))
    #self.img.setImage(self.dataModel.receive())
    img, itype = self.dataModel.receive()
    if len(img.shape) == 3 and img.shape[0] == 1: img.shape=(img.shape[1], img.shape[2])
    if self.dataModel.ndata>15: self.graphicsViewStreamingData.setImage(img, autoRange=False, autoLevels=False)#, autoHistogramRange=False )
    else: 
      self.graphicsViewStreamingData.setImage(img, autoRange=False)
    #self.graphicsViewStreamingData.setImage(img[::-1,:])
    #self.img.setImage(img[::-1,:])
    #self.histogramAdjust.regionChanged() # This is a hack and probably slow. There is no autoLevel=False in pyqtgraph, hence this solution. May file a enchancement.
    #print(self.histogramAdjust.region.getRegion())


  #@pyqtSlot()
  #def buttonTestView_clicked(self):
  #  if self.qtimerOn is True:
  #    self.qtimer.stop()
  #    self.qtimerOn = False
  #    endTime = time.time()
  #    self.labelStatusInfo.setText('Projection acquisition stopped: {} fps'.
  #                  format(self.mockData.ndata/(endTime-self.begTime)))
  #    self.mockData.ndata=0
  #  else:
  #    self.qtimer.start(0)
  #    self.qtimerOn = True
  #    self.begTime = time.time()
  #    self.labelStatusInfo.setText('Projection acquisition started')


  def excepthook(self, e_type, e_value, e_tb):
    # ignore KeyboardInterrupt
    if issubclass(e_type, KeyboardInterrupt):
      print("KeyboardException silenced: {} {} {}".format(e_type, e_value, e_tb))
    else: print("Silenced exception: {}".format(e_type))
    return

if __name__ == "__main__":
  import sys
  app = QtWidgets.QApplication(sys.argv)
  tsView = TSViewDAQ()
  #sys.excepthook = tsView.excepthook
  tsView.show()
  sys.exit(app.exec_())

  
