from multiprocessing import Process
import subprocess
import random
import time
from argparse import ArgumentParser


# Arguments
parser = ArgumentParser()
parser.add_argument("-cmd", "--command", dest="cmd",
  help="Full path of the zmq performance command, e.g. \'/home/beams/TBICER/Downloads/libzmq/perf/local_thr\'")
parser.add_argument("-mnum", "--message_number", dest="mnum", type=int,
  help="The number of messages to send, e.g. \'10000000\'.")
parser.add_argument("-msize", "--message_size", dest="msize", type=int,
  help="The size of each message, e.g. \'4096\'.")
parser.add_argument("-iport", "--initial_port", dest="iport", type=int,
  help="Initial port number, e.g. \'50000\'.")
parser.add_argument("-pnum", "--process_number", dest="pnum", type=int,
  help="The number of messages to send, e.g. \'2\'.")
parser.add_argument("-addr", "--local_address", dest="address",
  help="The local ip address, e.g. \'164.54.113.150\'.")

args = parser.parse_args()


msize = args.msize
mnum = args.mnum
cmd = args.cmd #'/home/beams/TBICER/Downloads/libzmq/perf/local_thr' # server

iport = args.iport #50000 # starting port number
pnum = args.pnum #4 # num parallel transfers

#address = '164.54.113.157' # mona4
address = args.address #'164.54.113.150' # mona3

def init_transfer(cmd, remote_address, port, msg_size, msg_num):
  remote = "tcp://{}:{}".format(remote_address, port)
  print("Remote address: {}; msg_size={}; msg_num={}; total(MiB)={}".format(remote, msg_size, msg_num, msg_size*msg_num/(1024*1024.)))
  result = subprocess.run([cmd, remote, str(msg_size), str(msg_num)], stdout=subprocess.PIPE)
  print("{}:\n{}".format(remote, result.stdout))
  return result

processes = []

for i in range(pnum):
  p = Process(target=init_transfer, args=(cmd, address, str(iport+i), msize, mnum))
  p.start()
  print("Started process {}; id={}".format(i, p))
  processes.append(p)

beg = time.time()
for p in processes:
  print("Waiting process {}".format(p))
  p.join()
end = time.time()

# calculate bw
dsize = mnum*msize*pnum/(1024*1024.) # MiB
elapsed = end-beg # secs
print("Bandwidth (MiB/s)={}; Total data (MiB)={}; Total time (sec)={}; num. processes={}".format(dsize/elapsed, dsize, elapsed, pnum))
