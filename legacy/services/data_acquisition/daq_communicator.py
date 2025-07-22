import zmq
import traceback as tb
from logger_config import logger

# create a class
class DAQCommunication:
  def __init__(self, data_plane_addr="tcp://*:50000", data_plane_hwm=0, 
               control_plane_addr="tcp://*:50001"):
    """
    Initializes the DAQCommunicator with the given addresses. There are two
     sockets: 
     data_plane_addr, a publisher socket that streams data.  This socket can
     losse msgs but it is high performant.
     control_plane_addr, a router socket that is used for sending/receiving 
     control/metadata messages.

    Args:
      data_plane_addr (str):    The address for the data plane publisher socket.
                                Default is "tcp://*:50000".
      publisher_hwm (int):      The high water mark for the publisher socket. Default
                                is 0.
      control_plane_addr (str): The address for the control plane router socket.
                                Default is "tcp://*:50001".

    Attributes:
      context (zmq.Context): The ZeroMQ context for creating sockets.
      data_plane_socket (zmq.Socket): The publisher socket for the data plane.
      control_socket (zmq.Socket): The router socket for the control plane.
      subscribers (set): A set to keep track of subscribers.
    """
    self.context = zmq.Context()#(io_threads=2)

    self.data_plane_socket = self.context.socket(zmq.PUB)
    self.data_plane_socket.setsockopt(zmq.SNDHWM, data_plane_hwm)
    self.data_plane_socket.bind(data_plane_addr)

    self.control_socket = self.context.socket(zmq.ROUTER)
    self.control_socket.bind(control_plane_addr)
    self.subscribers = set()
    self.socket_poller = zmq.Poller()
    self.socket_poller.register(self.control_socket, zmq.POLLIN)

  def __enter__(self):
    return self

  def __exit__(self, exc_type, exc_value, traceback):
    if exc_type:
      logger.error(f"An exception occurred; type: {exc_type}, value: {exc_value}")
      logger.error(f"Traceback: {tb.print_tb(traceback)}")
    logger.error("Exiting context: DAQCommunication")
    self.finalize()

  def finalize(self):
    try:
      self.data_plane_socket.send("end_data".encode()) # send termination signal
    except Exception as e:
      logger.error(f"Error while sending termination signal: {e}")
    finally:
      logger.info("Finalizing DAQCommunication...")
      self.data_plane_socket.close()
      self.control_socket.close()
      self.context.term()
      self.data_plane_socket = None
      self.control_socket = None
      self.context = None
      self.socket_poller = None

  def check_control_plane(self):
    """
    Checks the control plane for incoming messages on the control socket.

    This method runs an infinite loop that checks the messages received on control plane.  
    It acts according to the message received, if it is a 'SUBSCRIBE' message, the sender's
    identity is added to the set of subscribers and an acknowledgment ('ACK') is sent back.

    If the message is not recognized, a warning is logged.

    Raises:
      zmq.Again: If no message is available to be received, the loop breaks and the method exits.
    """
    while True:
      try:
        sockets = dict(self.socket_poller.poll(timeout=100))  # Poll with a timeout
        if self.control_socket in sockets:
          identity, message = self.control_socket.recv_multipart(flags=zmq.NOBLOCK)
          if message == b'SUBSCRIBE':
            self.subscribers.add(identity)
            self.control_socket.send_multipart([identity, b'ACK'])
          else:
            logger.warning(f"Unknown message received: {message}; from (identity): {identity}")
        else:
          break
      except zmq.Again:
        break
      except Exception as e:
        logger.error(f"Error in check_control_plane: {e}")
        break
  
  def wait_for_terminal_input(self):
    while True:
      user_input = input("Enter 'y' to continue: ")
      if user_input.lower() == "y":
        logger.info(f"Number of subscribers registered: {len(self.subscribers)}\n Continuing...")
        break
      else:
        logger.warning(f"Unknown user input: {user_input}")