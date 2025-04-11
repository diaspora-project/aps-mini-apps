import numpy as np
import json
import mochi.mofka.client as mofka
import time
from collections import deque

def generate_worker_msgs(data: np.ndarray, dims: list, projection_id: int, theta: float,
                         n_ranks: int, center: float, seq: int) -> list:
    nsin = dims[0] // n_ranks  # Sinograms per rank
    remaining = dims[0] % n_ranks  # Remaining sinograms
    msgs = []
    curr_sinogram_id = 0
    for i in range(n_ranks):
        r = 1 if remaining > 0 else 0
        remaining -= 1
        data_size = data.dtype.itemsize*(nsin + r) * dims[1]
        # Prepare the message for the worker
        msg = prepare_data_rep_msg(seq,
                                   projection_id,
                                   theta,
                                   center,
                                   data_size,
                                   data[curr_sinogram_id*dims[1]:(curr_sinogram_id+(nsin+r))*dims[1]]
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

class MofkaDist:

    def __init__(self, group_file: str, batchsize: int = 16):
        # setup mofka
        self.driver = mofka.MofkaDriver(group_file, use_progress_thread=True)
        self.seq = 0
        self.nranks = 1
        self.buffer = []
        self.counter = 0
        self.batch = batchsize
        self.futures = deque()

    def producer(self, topic_name: str, producer_name: str) -> mofka.Producer:
        topic = self.driver.open_topic(topic_name)
        batchsize = self.batch #self.batch #mofka.AdaptiveBatchSize
        thread_pool = mofka.ThreadPool(1)
        ordering = mofka.Ordering.Strict
        producer = topic.producer(producer_name,
                                  batch_size=batchsize,
                                  thread_pool=thread_pool,
                                  ordering=ordering)
        return producer

    def consumer(self, topic_name: str, consumer_name: str) -> mofka.Consumer:
        batch_size = self.batch #mofka.AdaptiveBatchSize
        thread_pool = mofka.ThreadPool(0)
        topic = self.driver.open_topic(topic_name)
        consumer = topic.consumer(name=consumer_name,
                                  thread_pool=thread_pool,
                                  batch_size=batch_size)
        return consumer


    def handshake(self, nproc_sirt: int,  row: int, col: int) -> str :
        # Figure out how many ranks are there at the remote location
        if nproc_sirt == 0:
            topic_name = "handshake_s_d"
            topic = self.driver.open_topic(topic_name)
            consumer = topic.consumer(name="handshaker",
                                    thread_pool=mofka.ThreadPool(0),
                                    batch_size=self.batch)
            f = consumer.pull()
            event = f.wait()
            self.nranks = json.loads(event.metadata)["comm_size"]
            self.seq += 1
            del event
            del consumer
            del topic
        elif nproc_sirt< 0:
            raise ValueError('Number of reconstruction processes cannot be negative')
        else:
            self.nranks = nproc_sirt
        topic_name = "handshake_d_s"
        producer = self.producer(topic_name, "handshaker")
        # distribute data info
        for p in range(self.nranks):
            info = assign_data(p, self.nranks, row, col)
            f = producer.push(info)
        producer.flush()
        self.seq += 1
        del producer
        return "Done"

    def pull_image(self, consumer: mofka.Consumer):
        ts = time.perf_counter()
        f = consumer.pull()
        event = f.wait()
        t_wait = time.perf_counter()
        mofka_metadata = event.metadata
        t_meta = time.perf_counter()
        print("metadata retreived ", mofka_metadata, flush=True)
        mofka_data = bytearray(event.data[0])
        t_data = time.perf_counter()
        return json.loads(mofka_metadata), mofka_data, [t_wait - ts, t_meta- t_wait, len(str(mofka_metadata)), t_data - t_meta, len(mofka_data)]

    def push_image(self, data: np.ndarray, row :int, col: int,
                   theta: float, projection_id: int, center: float,
                   producer: mofka.Producer) -> int :
        dims = [row, col]

        center = (dims[1] / 2.0) if center == 0.0 else center
        print(f"Sending proj: id={projection_id}; center={center}; dims[0]={dims[0]}; dims[1]={dims[1]}; theta={theta}")

        # Generate worker messages
        msgs = generate_worker_msgs(data,
                                    dims,
                                    projection_id,
                                    theta,
                                    self.nranks,
                                    center,
                                    self.seq)
        self.buffer.append(msgs)
        mofka_t = []
        # Send data to workers
        for i in range(self.nranks):
            ts = time.perf_counter()
            f = producer.push(self.buffer[self.counter][i][0], self.buffer[self.counter][i][1])
            #f.wait()
            # if self.counter % self.batch == 0:
            #     self.futures.append(f)
            mofka_t.append(["push", projection_id, ts, time.perf_counter(), time.perf_counter() - ts, len(str(self.buffer[self.counter][i][0])) ,len(self.buffer[self.counter][i][1])])

        self.seq += 1
        self.counter += 1
        if self.counter == 2*self.batch:
            # ts = time.perf_counter()
            # #producer.flush()
            # for i in range(self.nranks):
            #     self.futures[0].wait()
            #     self.futures.popleft()
            #     mofka_t.append(["wait", projection_id, ts, time.perf_counter(), time.perf_counter() - ts, self.nranks*len(self.buffer)* len(str(self.buffer[self.counter-1][0][0])), self.nranks*len(self.buffer)*len(self.buffer[self.counter-1][0][1])])
            self.buffer = self.buffer[self.batch:]
            self.counter = self.counter - self.batch

        return mofka_t

    def last_flush(self, producer):
        if len(self.buffer)> 0:
            ts = time.perf_counter()
            producer.flush()
            # while not self.futures:
            #     self.futures[0].wait()
            #     self.futures.popleft()
            self.seq += 1
            return ["last_flush","" , ts, time.perf_counter(), time.perf_counter() - ts, self.nranks*len(self.buffer)* len(str(self.buffer[self.counter-1][0][0])), self.nranks*len(self.buffer)*len(self.buffer[self.counter-1][0][1])]
        return None

    def done_image(self, producer) -> int:
        msg_metadata = {"Type": "FIN" }
        # Send Fin message to workers
        for _ in range(self.nranks):
            producer.push(msg_metadata, bytearray(1))
        producer.flush()
        self.seq += 1
        return 0

    def finalize(self):
        del self.driver
        del self.buffer