import argparse
import numpy as np
import zmq
import time
import os
import sys
sys.path.append(os.path.join(os.path.dirname(__file__), './local'))
import flatbuffers
import TraceSerializer

def parse_arguments():
  parser = argparse.ArgumentParser( description='Data Acquisition Process')
  parser.add_argument('--synchronize_subscriber', action='store_true',
      help='Synchronizes this subscriber to publisher (publisher should wait for subscriptions)')
  parser.add_argument('--subscriber_hwm', type=int, default=10*1024, 
      help='Sets high water mark value for this subscriber.')
  parser.add_argument('--publisher_address', default=None,
      help='Remote publisher address')
  parser.add_argument('--publisher_rep_address',
      help='Remote publisher REP address for synchronization')
  return parser.parse_args()


def synchronize_subs(context, publisher_rep_address):
  sync_socket = context.socket(zmq.REQ)
  sync_socket.connect(publisher_rep_address)

  sync_socket.send(b'') # Send synchronization signal
  sync_socket.recv() # Receive reply
  

def main():
  args = parse_arguments()

  context = zmq.Context()

  # Subscriber setup
  subscriber_socket = context.socket(zmq.SUB)
  subscriber_socket.set_hwm(args.subscriber_hwm)
  subscriber_socket.connect(args.publisher_address)
  subscriber_socket.setsockopt(zmq.SUBSCRIBE, b'')

  if args.synchronize_subscriber:
    synchronize_subs(context, args.publisher_rep_address)


  # Setup flatbuffer builder and serializer
  builder = flatbuffers.Builder(0)
  serializer = TraceSerializer.ImageSerializer(builder)

  # Receive images
  total_received=0
  total_size=0
  total_numb_projs = 1440

  time0 = time.time()
  while True:
    msg = subscriber_socket.recv()
    if msg == b"end_data": break # End of data acquisition

    # Deserialize msg to image
    read_image = serializer.deserialize(serialized_image=msg)
    # Check if serialized and original data are same
    my_image = read_image.TdataAsNumpy()
    #dims=(1920,1200)
    my_image.dtype = np.uint16
    #my_image = np.reshape(my_image, dims)
    uniqueId=read_image.UniqueId()
    if not ((my_image[0] == uniqueId) and 
            (my_image[-1] == total_numb_projs-uniqueId)) : 
                print("Values not correct {} != {}".format(my_image[0], uniqueId))
    total_received += 1
    total_size += len(msg)
  time1 = time.time()
    
  print("Received number of projections: {}".format(total_received))
  print("Rate = {} MB/sec; {} msg/sec".format((total_size/(2**20))/(time1-time0), total_received/(time1-time0)))



if __name__ == '__main__':
  main()
  
