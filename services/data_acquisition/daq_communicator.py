import zmq
import traceback as tb

# create a class
class DAQCommunication:
  def __init__(self, publisher_addr="tcp://*:50000", synch_addr=None, synch_count=1, publisher_hwm=0):

    self.context = zmq.Context()#(io_threads=2)

    # Set up the publisher socket
    self.publisher_socket = self.context.socket(zmq.PUB)
    self.publisher_socket.set_hwm(publisher_hwm)
    self.publisher_socket.bind(publisher_addr)

    # Set up the synchronization socket and wait for subscribers to join
    if synch_addr is not None:
      self.sync_socket = self.context.socket(zmq.REP)
      self.sync_socket.bind(self.args.synch_addr)
      self.synchronize_subs(synch_count)


  def __enter__(self):
    print("Entering context: DAQCommunication")
    return self


  def __exit__(self, exc_type, exc_value, traceback):
    if exc_type:
      print(f"An exception occurred; type: {exc_type}, value: {exc_value}")
      print(f"Traceback: {tb.print_tb(traceback)}")
    
    print("Exiting context: DAQCommunication")
    self.finalize()

  def finalize(self):
    print("Cleaning up resources...")
    self.publisher_socket.send("end_data".encode()) # send termination signal

    # Clean up resources
    self.publisher_socket.close()
    if hasattr(self, 'sync_socket'): self.sync_socket.close()
    self.context.term()

  # 1. Synchronize/handshake with remote
  def synchronize_subs(self, synch_count):
    counter = 0
    print("Waiting {} subscriber(s) to synchronize...".format(synch_count))
    while counter < synch_count:
      self.sync_socket.recv() # wait for subscriber
      self.sync_socket.send(b'') # reply ack
      counter += 1
      print("Subscriber joined: {}/{}".format(counter, synch_count))