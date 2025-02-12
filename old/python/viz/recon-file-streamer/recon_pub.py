import zmq
import h5py
import time
import glob
from stat import S_ISREG, ST_CTIME, ST_MODE
import os, sys, time
import numpy

#python recon_pub.py /projects/BrainImagingADSP/bicer/dynamic/alum-foam/d_alum-foam-l32-s1-n0-i1/output


context = zmq.Context()

socket = context.socket(zmq.PUB)
#socket.bind("tcp://140.221.68.108:9500")
socket.connect("tcp://handyn.xray.aps.anl.gov:9999")


# path to the directory (relative or absolute)
dirpath = sys.argv[1] if len(sys.argv) == 2 else r'.'
prev_entry = None

mini_loop = 0

while True:
  time.sleep(2)
  print("Checking for new files")

  # get all entries in the directory w/ stats
  #entries = (os.path.join(dirpath, fn) for fn in os.listdir(dirpath))
  entries = (os.path.join(dirpath, fn) for fn in glob.glob(dirpath+"/*.h5"))
  entries = ((os.stat(path), path) for path in entries)

  # leave only regular files, insert creation date
  entries = ((stat[ST_CTIME], path)
           for stat, path in entries if S_ISREG(stat[ST_MODE]))

  sorted_entries = sorted(entries)

  if len(sorted_entries) < 2:
    print("No new files detected")
    continue

  new_entry = sorted_entries[-2]
  new_entry = new_entry[1]
  if new_entry == prev_entry:
    print("No new files detected")
    # remove when recon step is working
    """
    f = h5py.File(sorted_entries[mini_loop % len(sorted_entries)][1])
    data = numpy.array(f["/data"])
    print(data)
    socket.send_pyobj(data)
    f.close()
    mini_loop = mini_loop + 1
    """
    continue

  prev_entry = new_entry
  print("Processing new entry:", new_entry)

  f = h5py.File(new_entry)
  data = numpy.array(f["/data"])
  print(data)

  socket.send_pyobj(data)
  f.close()

  
  



