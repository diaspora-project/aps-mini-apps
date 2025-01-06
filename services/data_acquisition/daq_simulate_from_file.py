import os
import sys
import time
import signal
from logger_config import logger
import numpy as np
import h5py as h5

class DAQSimulator:
  def __init__(self):
    self.bsignal = False
    signal.signal(signal.SIGINT, self._signal_handler)

  def _signal_handler(self, sig, frame):
    self.bsignal = True
    logger.info(f"Signal handler called with signal {sig}")

  def _read_hdf5_file(self, input_f):
    if not os.path.exists(input_f):
      logger.error(f"File not found: {input_f}")
      sys.exit(1)

    logger.info(f"Loading tomography data: {input_f}")
    t0 = time.time()
    with h5.File(input_f, 'r') as f:
      idata = f['exchange/data'][()]
      flat = f['exchange/data_white'][()]
      dark = f['exchange/data_dark'][()]
      itheta = f['exchange/theta'][()]
    logger.info("Projection dataset IO time={:.2f}; dataset shape={}; size={}; Theta shape={};".format(
      time.time() - t0, idata.shape, idata.size, itheta.shape))
    return idata, flat, dark, itheta

  def _convert_radian(self, itheta):
    m = np.max(itheta)
    if m > 2 * np.pi:
      logger.info(f"Theta values are in degree (max={m}); converting to radian.")
      itheta = itheta * np.pi / 180
    return itheta

  def _setup_simulation_data(self, input_f, beg_sinogram=0, num_sinograms=0):
    idata, flat, dark, itheta = self._read_hdf5_file(input_f)

    idata = np.array(idata, dtype=np.float32)
    if flat is not None:
      flat = np.array(flat, dtype=np.float32)
    if dark is not None:
      dark = np.array(dark, dtype=np.float32)
    if itheta is not None:
      itheta = np.array(itheta, dtype=np.float32)

    itheta = self._convert_radian(itheta)

    return idata, flat, dark, itheta

  def _ordered_subset(self, max_ind, nelem):
    nsubsets = np.floor(max_ind / nelem).astype(int)
    all_arr = np.array([])
    for i in np.arange(nsubsets):
      all_arr = np.append(all_arr, np.arange(start=i, stop=max_ind, step=nsubsets))
    return all_arr.astype(int)

  def _serialize_dataset(self, idata, flat, dark, itheta, seq=0):
    data = []
    start_index = 0
    time_ser = 0.
    serializer = TraceSerializer.ImageSerializer()

    logger.info("Starting serialization")
    if flat is not None:
      for uniqueFlatId, flatId in zip(range(start_index, start_index + flat.shape[0]), range(flat.shape[0])):
        t_ser0 = time.time()
        dflat = flat[flatId]
        itype = serializer.ITypes.WhiteReset if flatId == 0 else serializer.ITypes.White
        serialized_data = serializer.serialize(image=dflat, uniqueId=uniqueFlatId,
                             itype=itype,
                             rotation=0, seq=seq)
        data.append(serialized_data)
        time_ser += time.time() - t_ser0
        seq += 1
      start_index += flat.shape[0]

    if dark is not None:
      for uniqueDarkId, darkId in zip(range(start_index, start_index + dark.shape[0]), range(dark.shape[0])):
        t_ser0 = time.time()
        dflat = dark[flatId]
        itype = serializer.ITypes.DarkReset if darkId == 0 else serializer.ITypes.Dark
        serialized_data = serializer.serialize(image=dflat, uniqueId=uniqueDarkId,
                             itype=itype,
                             rotation=0, seq=seq)
        time_ser += time.time() - t_ser0
        seq += 1
        data.append(serialized_data)
      start_index += dark.shape[0]

    for uniqueId, projId, rotation in zip(range(start_index, start_index + idata.shape[0]),
                        range(idata.shape[0]), itheta):
      t_ser0 = time.time()
      proj = idata[projId]
      itype = serializer.ITypes.Projection
      serialized_data = serializer.serialize(image=proj, uniqueId=uniqueId,
                           itype=itype,
                           rotation=rotation, seq=seq)
      time_ser += time.time() - t_ser0
      seq += 1
      data.append(serialized_data)

    logger.info("Serialization time={:.2f}".format(time_ser))
    return np.array(data, dtype=object)

  def simulate_from_file(self, publisher_socket, input_f,
               beg_sinogram=0, num_sinograms=0, seq=0, slp=0,
               iteration=1, save_after_serialize=False, prj_slp=0):
    serialized_data = None
    if input_f.endswith('.npy'):
      serialized_data = np.load(input_f, allow_pickle=True)
    else:
      idata, flat, dark, itheta = self._setup_simulation_data(input_f, beg_sinogram, num_sinograms)
      serialized_data = self._serialize_dataset(idata, flat, dark, itheta)
      if save_after_serialize:
        np.save("{}.npy".format(input_f), serialized_data)
      del idata, flat, dark

    tot_transfer_size = 0
    time0 = time.time()
    nelems_per_subset = 16
    indices = self._ordered_subset(serialized_data.shape[0],
                    nelems_per_subset)
    for it in range(iteration):
      logger.info("Current iteration over dataset: {}/{}".format(it + 1, iteration))
      for index in indices:
        if self.bsignal:
          while True:
            cmd = input("\nSignal received, (q)uit or (c)ontinue: ")
            if cmd == 'q' or cmd == 'c':
              break
            else:
              logger.error("Unknown command: {}".format(cmd))
          if cmd == 'q':
            logger.info("Exiting...")
            sys.exit(0)
          if cmd == 'c':
            self.bsignal = False
            logger.info("Continue streaming projections.")

        logger.info("Sending projection {}".format(index))
        time.sleep(prj_slp)
        dchunk = serialized_data[index]
        publisher_socket.send(dchunk, copy=False)
        tot_transfer_size += len(dchunk)
      time.sleep(slp)
    time1 = time.time()

    elapsed_time = time1 - time0
    tot_MiBs = (tot_transfer_size * 1.) / 2 ** 20
    nproj = iteration * len(serialized_data)
    logger.info("Sent number of projections: {}; Total size (MiB): {:.2f}; Elapsed time (s): {:.2f}".format(nproj, tot_MiBs, elapsed_time))
    logger.info("Rate (MiB/s): {:.2f}; (msg/s): {:.2f}".format(tot_MiBs / elapsed_time, nproj / elapsed_time))

    return seq

## Define a signal handler
#bsignal = False
#def signal_handler(sig, frame):
  #global bsignal
  #logger.info(f"Signal handler called with signal {sig}")
  #bsignal = True 

## Register the signal handler
#signal.signal(signal.SIGINT, signal_handler)

## read hdf5 file
#def read_hdf5_file(input_f):
  ## check if the file exists
  #if not os.path.exists(input_f):
    #logger.error(f"File not found: {input_f}")
    #sys.exit(1)

  #logger.info(f"Loading tomography data: {input_f}")
  #t0=time.time()
  #with h5.File(input_f, 'r') as f:
    #idata = f['exchange/data'][()]
    #flat = f['exchange/data_white'][()]
    #dark = f['exchange/data_dark'][()]
    #itheta = f['exchange/theta'][()]
  #logger.info("Projection dataset IO time={:.2f}; dataset shape={}; size={}; Theta shape={};".format(
                             #time.time()-t0 , idata.shape, idata.size, itheta.shape))
  #return idata, flat, dark, itheta

## convert theta values to radian
#def convert_radian(itheta):
  #m = np.max(itheta)
  #if m>2*np.pi:
    #logger.info(f"Theta values are in degree (max={m}); converting to radian.")
    #itheta = itheta*np.pi/180
  #return itheta


#def setup_simulation_data(input_f, beg_sinogram=0, num_sinograms=0):
  #idata, flat, dark, itheta = read_hdf5_file(input_f)

  #idata = np.array(idata, dtype=np.float32) #dtype('uint16'))
  #if flat is not None: flat = np.array(flat, dtype=np.float32) #dtype('uint16'))
  #if dark is not None: dark = np.array(dark, dtype=np.float32) #dtype('uint16'))
  #if itheta is not None: itheta = np.array(itheta, dtype=np.float32) #dtype('float32'))

  #itheta = convert_radian(itheta) 

  #return idata, flat, dark, itheta

#def ordered_subset(max_ind, nelem):
  #nsubsets = np.ceil(max_ind/nelem).astype(int)
  #all_arr = np.array([])
  #for i in np.arange(nsubsets):
    #all_arr = np.append(all_arr, np.arange(start=i, stop=max_ind, step=nsubsets))
  #return all_arr.astype(int)

#def serialize_dataset(idata, flat, dark, itheta, seq=0):
  #data = []
  #start_index=0
  #time_ser=0.
  #serializer = TraceSerializer.ImageSerializer()

  #logger.info("Starting serialization")
  #if flat is not None:
    #for uniqueFlatId, flatId in zip(range(start_index, 
                             #start_index+flat.shape[0]), range(flat.shape[0])):
      #t_ser0 =  time.time()
      #dflat = flat[flatId]
      #itype = serializer.ITypes.WhiteReset if flatId==0 else serializer.ITypes.White
      #serialized_data = serializer.serialize(image=dflat, uniqueId=uniqueFlatId, 
                                        #itype=itype,
                                        #rotation=0, seq=seq)
      #data.append(serialized_data)
      #time_ser += time.time()-t_ser0
      #seq+=1
    #start_index+=flat.shape[0]

  ## dark data
  #if dark is not None:
    #for uniqueDarkId, darkId in zip(range(start_index, start_index+dark.shape[0]), 
                                    #range(dark.shape[0])):
      #t_ser0 =  time.time()
      ##builder.Reset()
      #dflat = dark[flatId]
      ##serializer = TraceSerializer.ImageSerializer(builder)
      #itype = serializer.ITypes.DarkReset if darkId==0 else serializer.ITypes.Dark
      #serialized_data = serializer.serialize(image=dflat, uniqueId=uniqueDarkId, 
                                        #itype=itype,
                                        #rotation=0, seq=seq) #, center=10.)
      #time_ser += time.time()-t_ser0
      #seq+=1
      #data.append(serialized_data)
    #start_index+=dark.shape[0]

  ## projection data
  #for uniqueId, projId, rotation in zip(range(start_index, start_index+idata.shape[0]), 
                                        #range(idata.shape[0]), itheta):
    #t_ser0 =  time.time()
    #proj = idata[projId]
    #itype = serializer.ITypes.Projection
    #serialized_data = serializer.serialize(image=proj, uniqueId=uniqueId,
                                      #itype=itype,
                                      #rotation=rotation, seq=seq) #, center=10.)
    #time_ser += time.time()-t_ser0
    #seq+=1
    #data.append(serialized_data)

  #logger.info("Serialization time={:.2f}".format(time_ser))
  #return np.array(data, dtype=object)
  

#def simulate_from_file(publisher_socket, input_f, 
                      #beg_sinogram=0, num_sinograms=0, seq=0, slp=0,
                      #iteration=1, save_after_serialize=False, prj_slp=0):
  #global bsignal

  #serialized_data = None
  #if input_f.endswith('.npy'):
    #serialized_data = np.load(input_f, allow_pickle=True)
  #else:
    #idata, flat, dark, itheta = setup_simulation_data(input_f, beg_sinogram, num_sinograms)
    #serialized_data = serialize_dataset(idata, flat, dark, itheta)
    #if save_after_serialize: np.save("{}.npy".format(input_f), serialized_data)
    #del idata, flat, dark
  ##logger.info("data shape={}; bytes={}; type={}; serialized_data_len={}".format(serialized_data.shape, serialized_data.nbytes, type(serialized_data[0]), len(serialized_data[0])))
  ## serialized_data[0] is a byte array
  #tot_transfer_size=0
  #start_index=0
  #time0 = time.time()
  #nelems_per_subset = 16
  #indices = ordered_subset(serialized_data.shape[0], 
                              #nelems_per_subset)
  #for it in range(iteration): # Simulate data acquisition
    #logger.info("Current iteration over dataset: {}/{}".format(it+1, iteration))
    #for index in indices:
      ## Check if signal received
      #if bsignal:
        #while True:
          #cmd = input("\nSignal received, (q)uit or (c)ontinue: ")
          #if cmd == 'q' or cmd == 'c': break
          #else: logger.error("Unknown command: {}".format(cmd))
        #if cmd == 'q':
          #logger.info("Exiting...")
          #sys.exit(0)
        #if cmd == 'c':
          #bsignal = False
          #logger.info("Continue streaming projections.")

    ##for dchunk in serialized_data:
      ##logger.info("Sending projection {}; sleep time={}".format(index, prj_slp))
      #logger.info("Sending projection {}".format(index))
      #time.sleep(prj_slp)
      #dchunk = serialized_data[index]
      #publisher_socket.send(dchunk, copy=False)
      #tot_transfer_size+=len(dchunk)
    #time.sleep(slp)
  #time1 = time.time()

  #elapsed_time = time1-time0
  #tot_MiBs = (tot_transfer_size*1.)/2**20
  #nproj = iteration*len(serialized_data)
  #logger.info("Sent number of projections: {}; Total size (MiB): {:.2f}; Elapsed time (s): {:.2f}".format(nproj, tot_MiBs, elapsed_time))
  #logger.info("Rate (MiB/s): {:.2f}; (msg/s): {:.2f}".format(tot_MiBs/elapsed_time, nproj/elapsed_time))

  #return seq