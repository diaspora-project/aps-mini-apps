import numpy as np
import json
from pymargo.core import Engine
import mochi.mofka.client as mofka

def generate_worker_msgs(data: np.ndarray, dims: list, projection_id: int, theta: float,
                         n_ranks: int, center: float, seq: int) -> list:
    nsin = dims[1] // n_ranks  # Sinograms per rank
    remaining = dims[1] % n_ranks  # Remaining sinograms
    msgs = []
    curr_sinogram_id = 0
    for i in range(n_ranks):
        r = 1 if remaining > 0 else 0
        remaining -= 1
        data_size = data.dtype.itemsize*(nsin + r) * dims[1]

        # Prepare the message for the worker
        msg = prepare_data_rep_msg(
            seq, projection_id, theta, center, data_size, data[curr_sinogram_id:curr_sinogram_id+(nsin+r)*dims[1]]
        )
        msgs.append(msg)
        curr_sinogram_id += (nsin + r)

    return msgs

def prepare_data_rep_msg(seq: int, projection_id: int, theta: float,
                         center: float, data_size: int,
                         data: np.ndarray) -> list:
    """Prepare the data reply message similar to the C function."""
    # Create the metadata/data part of the message
    msg_metadata = {"Type": "MSG_DATA_REP",
                    "seq_n": seq,
                    "data_size": data_size,
                    "projection_id": projection_id,
                    "theta": theta,
                    "center": center,
                    "dtype": str(data.dtype)}
    msg_data = bytearray(data)
    return [msg_metadata, msg_data]


def assign_data(comm_rank: int, comm_size: int, tot_sino: int, tot_cols: int) -> dict:
    nsino = tot_sino // comm_size
    remaining = tot_sino % comm_size

    r = 1 if comm_rank < remaining else 0
    my_nsino = r + nsino
    beg_sino = (1 + nsino) * comm_rank if comm_rank < remaining else (1 + nsino) * remaining + nsino * (comm_rank - remaining)

    # Create and return the data info dict
    info_rep = {
        "Type" : "MSG_DATAINFO_REQ",
        "tn_sinograms" : tot_sino,
        "beg_sinogram": beg_sino,
        "n_sinograms" : my_nsino,
        "n_rays_per_proj_row" : tot_cols
    }
    return info_rep

def data_selector(metadata, descriptor):
    return descriptor

def data_broker(metadata, descriptor):
    return [bytearray(descriptor.size)]

class MofkaDist:

    def __init__(self, mofka_protocol: str, group_file: str):
        # setup mofka
        self.engine = Engine(mofka_protocol)
        self.client = mofka.Client(self.engine)
        self.service = self.client.connect(group_file)
        self.seq = 0
        self.nranks = 1

    def producer(self, topic_name: str, producer_name: str) -> mofka.Producer:
        try:
            topic = self.service.create_topic(topic_name)
            self.service.add_memory_partition(topic_name, 0)
        except Exception as err:
            print("Exception ", err, " trying to open topic", topic_name)
        topic = self.service.open_topic(topic_name)
        batchsize = mofka.AdaptiveBatchSize
        thread_pool = mofka.ThreadPool(1)
        ordering = mofka.Ordering.Strict
        producer = topic.producer(producer_name, batchsize,
                                  thread_pool, ordering)
        return producer

    def consumer(self, topic_name: str, consumer_name: str) -> mofka.Consumer:
        batch_size = mofka.AdaptiveBatchSize
        thread_pool = mofka.ThreadPool(0)
        try:
            topic = self.service.create_topic(topic_name)
            self.service.add_memory_partition(topic_name, 0)
        except Exception as err:
            print("Exception ", err, " trying to open topic", topic_name)
        topic = self.service.open_topic(topic_name)
        consumer = topic.consumer(name=consumer_name,
                                  thread_pool=thread_pool,
                                  batch_size=batch_size,
                                  data_selector=data_selector,
                                  data_broker=data_broker)
        return consumer


    def handshake(self,  row: int , col: int) -> str :
        # Figure out how many ranks are there at the remote location
        print("start hanshake with sirt")
        topic = "handshake_s_d"
        consumer = self.consumer(topic, "handshaker")
        f = consumer.pull()
        event = f.wait()
        event.acknowledge()
        self.nranks = json.loads(event.metadata)["comm_size"]
        self.seq += 1
        topic = "handshake_d_s"
        producer = self.producer(topic, "handshaker")
        # distribute data info
        for p in range(self.nranks):
            info = assign_data(p, self.nranks, row, col)
            f = producer.push(info, data=bytearray(np.ones(1)))
            f.wait()
        self.seq += 1

        del event
        del consumer
        del producer
        del topic
        return "Done"

    def push_image(self, data: np.ndarray, row :int, col: int,
                   theta: float, projection_id: int, center: float,
                   producer: mofka.Producer) -> int :
        dims = [row, col]

        center = (dims[1] / 2.0) if center == 0.0 else center
        print(f"Sending proj: id={projection_id}; center={center}; dims[0]={dims[0]}; dims[1]={dims[1]}; theta={theta}")

        # Generate worker messages
        worker_msgs = generate_worker_msgs(data, dims, projection_id, theta,
                                           self.nranks, center, self.seq)

        # Send data to workers
        for i in range(self.nranks):
            curr_msg = worker_msgs[i]
            f = producer.push(curr_msg[0], curr_msg[1])
            f.wait()
        self.seq += 1
        return 0


    def done_image(self, producer) -> int:
        msg_metadata = {"Type": "FIN" }
        # Send Fin message to workers
        for i in range(self.nranks):
            f = producer.push(msg_metadata, "Done".encode("ascii"))
            f.wait()
        self.seq += 1
        return 0

    def init(self, mofka_protocol: str, group_file: str) -> mofka.ServiceHandle:
        self.__init__(mofka_protocol, group_file)
        return self.service

    def finalize(self):
        del self.service
        del self.client
        self.engine.finalize()
        del self.engine

