import zmq
from logger_config import logger
    
class DAQ_Comm:
    def __init__(self, context=None, bind_control_addr="tcp://*:50000", bind_data_addr="tcp://*:50001", data_hwm=None):
        """
        Initialize the ROUTER socket for control messages and a PUSH socket for data.
        Optionally set a custom data high-water mark (HWM).
        The PUSH socket is blocking, i.e., it will block if the buffer is full (as determined by the HWM).
        """
        self.bind_control_addr = bind_control_addr
        self.bind_data_addr = bind_data_addr

        if context == None: 
            self.context = zmq.Context()
        self.control_socket = self.context.socket(zmq.ROUTER)
        self.control_socket.bind(self.bind_control_addr)
        logger.info(f"DAQ Control ROUTER was bound on {self.bind_control_addr}")

        self.data_socket = self.context.socket(zmq.PUSH)
        if data_hwm is not None:
            self.data_socket.setsockopt(zmq.SNDHWM, data_hwm)
        self.data_socket.bind(self.bind_data_addr)
        logger.info(f"DAQ Data PUSH socket was bound on {self.bind_data_addr}; HWM set to {data_hwm if data_hwm is not None else 'default'}")

    def control_receive(self):
        msg_parts = self.control_socket.recv_multipart()
        identity, empty, message = msg_parts
        return identity, message

    def control_send(self, identity, reply):
        self.control_socket.send_multipart([identity, b'', reply])
    
    def data_send(self, message):
        self.data_socket.send(message)
    
    def data_send_multipart(self, parts):
        """Send a multipart message on the PUSH socket."""
        self.data_socket.send_multipart(parts)

    def close(self):
        self.control_socket.close()
        self.data_socket.close()
        self.context.term()
        logger.info("DAQ_Comm was cleaned up.")
