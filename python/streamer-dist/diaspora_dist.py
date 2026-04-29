import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '../common'))
import numpy as np
import json
import diaspora_stream.api as diaspora
import time
from collections import deque
from ts_collector import TimestampCollector

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

class DiasporaDist:

    def __init__(self, driver_type: str, driver_config_file: str = "", batchsize: int = 16):
        # setup diaspora
        driver_options = {}
        if driver_config_file != "":
            with open(driver_config_file) as f:
                driver_options = json.load(f)
        elif driver_type == "files":
            driver_options = {
                "root_path": "./diaspora-data"
            }
        self.driver = diaspora.Driver(backend=driver_type, options=driver_options)
        self.driver_type = driver_type
        self.seq = 0
        self.nranks = 1
        self.buffer = []
        self.counter = 0
        self.batch = batchsize
        self.futures = deque()
        self.ts = TimestampCollector()

    def producer(self, topic_name: str, producer_name: str) -> diaspora.Producer:
        topic = self.driver.open_topic(topic_name)
        batchsize = self.batch
        ordering = diaspora.Ordering.Strict
        kwargs = dict(batch_size=batchsize, ordering=ordering)
        if self.driver_type != 'files':
            kwargs['thread_pool'] = self.driver.make_thread_pool(1)
        return topic.producer(producer_name, **kwargs)

    def consumer(self, topic_name: str, consumer_name: str) -> diaspora.Consumer:
        batch_size = self.batch
        topic = self.driver.open_topic(topic_name)
        kwargs = dict(name=consumer_name, batch_size=batch_size)
        if self.driver_type != 'files':
            kwargs['thread_pool'] = self.driver.make_thread_pool(0)
        return topic.consumer(**kwargs)


    def handshake(self, nproc_sirt: int,  row: int, col: int) -> str :
        # Figure out how many ranks are there at the remote location
        if nproc_sirt == 0:
            topic_name = "handshake_s_d"
            topic = self.driver.open_topic(topic_name)
            hs_kwargs = dict(name="handshaker", batch_size=self.batch)
            if self.driver_type != 'files':
                hs_kwargs['thread_pool'] = self.driver.make_thread_pool(0)
            consumer = topic.consumer(**hs_kwargs)
            event = None
            while event is None:
                event = consumer.pull().wait(timeout_ms=-1)
            self.nranks = event.metadata["comm_size"]
            self.seq += 1
            del event
            consumer.unsubscribe()
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
        producer.flush().wait(timeout_ms=-1)
        self.seq += 1
        del producer
        return "Done"

    def pull_image(self, consumer: diaspora.Consumer):
        self.ts.record("PULL_START topic=daq_dist")
        f = consumer.pull()
        self.ts.record("PULL_END topic=daq_dist")
        self.ts.record("PULL_WAIT_START topic=daq_dist")
        event = f.wait(timeout_ms=-1)
        data_size = len(event.data[0]) if event.data else 0
        self.ts.record(f"PULL_WAIT_END topic=daq_dist,event_id={event.event_id},data_size={data_size}")
        metadata = event.metadata
        print("metadata retreived ", event.metadata, flush=True)
        data = bytearray(event.data[0])
        return metadata, data

    def push_image(self, data: np.ndarray, row :int, col: int,
                   theta: float, projection_id: int, center: float,
                   producer: diaspora.Producer) -> int :
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
        # Send data to workers
        for i in range(self.nranks):
            data_sz = len(self.buffer[self.counter][i][1])
            self.ts.record(f"PUSH_START topic=dist_sirt,data_size={data_sz}")
            producer.push(self.buffer[self.counter][i][0], self.buffer[self.counter][i][1])
            self.ts.record("PUSH_END topic=dist_sirt")

        self.seq += 1
        self.counter += 1

        if self.counter == 2*self.batch:
            # ts = time.perf_counter()
            # #producer.flush()
            # for i in range(self.nranks):
            #     self.futures[0].wait(timeout_ms=-1)
            #     self.futures.popleft()
            #     diaspora_t.append(["wait", projection_id, ts, time.perf_counter(), time.perf_counter() - ts, self.nranks*len(self.buffer)* len(str(self.buffer[self.counter-1][0][0])), self.nranks*len(self.buffer)*len(self.buffer[self.counter-1][0][1])])
            self.buffer = self.buffer[self.batch:]
            self.counter = self.counter - self.batch

    def last_flush(self, producer):
        if len(self.buffer)> 0:
            self.ts.record("FLUSH_START topic=dist_sirt")
            producer.flush()
            self.ts.record("FLUSH_END topic=dist_sirt")
            self.seq += 1

    def done_image(self, producer) -> int:
        msg_metadata = {"Type": "FIN" }
        # Send Fin message to workers
        for _ in range(self.nranks):
            self.ts.record("PUSH_START topic=dist_sirt,data_size=1")
            producer.push(msg_metadata, bytearray(1))
            self.ts.record("PUSH_END topic=dist_sirt")
        self.ts.record("FLUSH_START topic=dist_sirt")
        producer.flush()
        self.ts.record("FLUSH_END topic=dist_sirt")
        self.seq += 1
        return 0

    def finalize(self):
        del self.driver
        del self.buffer
