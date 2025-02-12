import time
from PyQt5 import QtCore, QtGui, QtWidgets
from PyQt5.QtCore import QObject, pyqtSlot
from PyQt5.QtWidgets import QWidget
from PyQt5.uic import loadUi
import pyqtgraph as pg


import numpy as np
import MockVizData
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
    self.viewBox = self.graphicsViewStreamingData.addViewBox()
    self.viewBox.setAspectLocked(True)

    self.img = pg.ImageItem(border='w')
    self.img.setOpts(axisOrder='row-major')
    self.histogramAdjust.setImageItem(self.img)

    self.img.setImage(np.random.randint(255, size=self.ddims, dtype=np.uint16))
    self.viewBox.addItem(self.img)
    self.viewBox.autoRange()

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

      if img.shape is not self.ddims:
        print("Shape is not correct")
        self.ddims=img.shape
        self.viewBox.setRange(QtCore.QRectF(0, 0, self.ddims[0], self.ddims[1]))
        img, itype = self.dataModel.receive()
        self.img.setImage(img[::-1,:])
      self.qtimer.start(0)
      self.qtimerOn = True
      self.snapTime= time.time()
      self.labelStatusInfo.setText('Started receiving data')


  def updateImage(self):
    #self.img.setImage(np.flipud(self.dataModel.receive()))
    #self.img.setImage(self.dataModel.receive())
    img, itype = self.dataModel.receive()
    self.img.setImage(img[::-1,:])
    self.histogramAdjust.regionChanged() # This is a hack and probably slow. There is no autoLevel=False in pyqtgraph, hence this solution. May file a enchancement.
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





if __name__ == "__main__":
    import sys
    app = QtWidgets.QApplication(sys.argv)
    tsView = TSViewDAQ()
    tsView.show()
    sys.exit(app.exec_())

  
