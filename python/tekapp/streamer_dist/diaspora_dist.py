import numpy as np
import json
import diaspora_stream.api as diaspora
import time
from collections import deque
from tekapp.common.ts_collector import TimestampCollector

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
        self.batch = batchsize
        # The most recently pushed batch is held alive on the Python side
        # until its push futures resolve. The C++ producer batch queue
        # runs on a background Argobots ULT and holds refs to the pushed
        # bytearrays until the batch is sent. If Python drops its refs
        # while C++ still holds them, the C++ release becomes the last
        # one and bytearray_dealloc runs on the producer thread without
        # the GIL — pymalloc is not thread-safe and the heap corrupts.
        # Holding one batch behind lets pull+compute on the next loop
        # iteration overlap with the producer ULT, so the subsequent
        # wait() is typically a no-op.
        self.prev_msgs = None
        self.prev_futures = None
        self.ts = TimestampCollector()

    def producer(self, topic_name: str, producer_name: str) -> diaspora.Producer:
        topic = self.driver.open_topic(topic_name)
        batchsize = self.batch
        ordering = diaspora.Ordering.Strict
        kwargs = dict(batch_size=batchsize, ordering=ordering)
        return topic.producer(producer_name, **kwargs)

    def consumer(self, topic_name: str, consumer_name: str) -> diaspora.Consumer:
        batch_size = self.batch
        topic = self.driver.open_topic(topic_name)
        kwargs = dict(name=consumer_name, batch_size=batch_size)
        return topic.consumer(**kwargs)


    def handshake(self, nproc_sirt: int,  row: int, col: int) -> str :
        # Figure out how many ranks are there at the remote location
        if nproc_sirt == 0:
            print("[dist.handshake] opening topic handshake_s_d", flush=True)
            topic_name = "handshake_s_d"
            topic = self.driver.open_topic(topic_name)
            print("[dist.handshake] creating consumer 'handshaker' on handshake_s_d", flush=True)
            hs_kwargs = dict(name="handshaker", batch_size=self.batch)
            consumer = topic.consumer(**hs_kwargs)
            print("[dist.handshake] waiting for SIRT to publish comm_size...", flush=True)
            # Correct pattern: pull() once to issue the request, then wait()
            # on the SAME future until it resolves. Calling pull() inside the
            # retry loop creates a new pending future each iteration and
            # discards the previous one, so an event delivered to attempt N's
            # future is lost when attempt N+1 replaces it.
            future = consumer.pull()
            event = None
            attempt = 0
            while event is None:
                attempt += 1
                print(f"[dist.handshake] wait attempt #{attempt}", flush=True)
                event = future.wait(timeout_ms=-1)
                if event is None:
                    time.sleep(0.1)
            self.nranks = event.metadata["comm_size"]
            print(f"[dist.handshake] received comm_size={self.nranks}", flush=True)
            self.seq += 1
            del event
            consumer.unsubscribe()
            del consumer
            del topic
        elif nproc_sirt< 0:
            raise ValueError('Number of reconstruction processes cannot be negative')
        else:
            self.nranks = nproc_sirt
            print(f"[dist.handshake] nproc_sirt provided as arg, nranks={self.nranks}", flush=True)
        print("[dist.handshake] opening producer on handshake_d_s (batch_size=1, unbuffered)", flush=True)
        topic_name = "handshake_d_s"
        topic = self.driver.open_topic(topic_name)
        producer = topic.producer("handshaker",
                                  batch_size=1,
                                  ordering=diaspora.Ordering.Strict)
        # distribute data info
        print(f"[dist.handshake] pushing {self.nranks} assignment messages on handshake_d_s", flush=True)
        for p in range(self.nranks):
            info = assign_data(p, self.nranks, row, col)
            f = producer.push(info)
        print("[dist.handshake] flushing handshake_d_s producer (wait -1)", flush=True)
        producer.flush().wait(timeout_ms=-1)
        print("[dist.handshake] flush done", flush=True)
        self.seq += 1
        del producer
        return "Done"

    def pull_image(self, consumer: diaspora.Consumer):
        self.ts.record("PULL_START topic=daq_dist")
        print("[dist.pull_image] calling consumer.pull() on daq_dist", flush=True)
        # pull() once, then wait() on that single future until it resolves.
        # Calling pull() each iteration would orphan the prior future and
        # lose any event already destined for it.
        f = consumer.pull()
        self.ts.record("PULL_END topic=daq_dist")
        self.ts.record("PULL_WAIT_START topic=daq_dist")
        event = None
        while event is None:
            event = f.wait(timeout_ms=-1)
            if event is None:
                time.sleep(0.1)
        data_size = len(event.data[0]) if event.data else 0
        self.ts.record(f"PULL_WAIT_END topic=daq_dist,event_id={event.event_id},data_size={data_size}")
        metadata = event.metadata
        print("metadata retreived ", event.metadata, flush=True)
        data = bytearray(event.data[0])
        return metadata, data

    def push_image(self, data: np.ndarray, row :int, col: int,
                   theta: float, projection_id: int, center: float,
                   producer: diaspora.Producer) -> int :
        # Drain the previous batch first: by the time the caller has
        # done pull+preprocess and re-entered push_image, the producer
        # ULT has had ample time to send it, so this wait is usually
        # a no-op. We MUST wait before dropping prev_msgs — see the
        # rationale in __init__.
        if self.prev_futures is not None:
            for f in self.prev_futures:
                f.wait(timeout_ms=-1)
            self.prev_futures = None
            self.prev_msgs = None

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
        # Send data to workers
        futures = []
        for i in range(self.nranks):
            data_sz = len(msgs[i][1])
            self.ts.record(f"PUSH_START topic=dist_sirt,data_size={data_sz}")
            futures.append(producer.push(msgs[i][0], msgs[i][1]))
            self.ts.record("PUSH_END topic=dist_sirt")

        # Hold strong refs to the bytearrays and their futures until
        # the next call (or last_flush) drains them.
        self.prev_msgs = msgs
        self.prev_futures = futures
        self.seq += 1

    def last_flush(self, producer):
        # Drain the final pending batch from push_image() before the
        # flush — both to release Python refs safely (main thread, GIL
        # held) and so the flush sees no leftover queued work.
        if self.prev_futures is not None:
            for f in self.prev_futures:
                f.wait(timeout_ms=-1)
            self.prev_futures = None
            self.prev_msgs = None
            self.ts.record("FLUSH_START topic=dist_sirt")
            f = producer.flush()
            self.ts.record("FLUSH_END topic=dist_sirt")
            self.ts.record("FLUSH_WAIT_START topic=dist_sirt")
            f.wait(timeout_ms=-1)
            self.ts.record("FLUSH_WAIT_END topic=dist_sirt")
            self.seq += 1

    def done_image(self, producer) -> int:
        msg_metadata = {"Type": "FIN" }
        # Hold strong refs to the FIN bytearrays until after flush
        # completes — otherwise the C++ release on the producer ULT
        # would trigger bytearray_dealloc without the GIL.
        fin_buffers = []
        # Send Fin message to workers
        for _ in range(self.nranks):
            b = bytearray(1)
            fin_buffers.append(b)
            self.ts.record("PUSH_START topic=dist_sirt,data_size=1")
            producer.push(msg_metadata, b)
            self.ts.record("PUSH_END topic=dist_sirt")
        self.ts.record("FLUSH_START topic=dist_sirt")
        f = producer.flush()
        self.ts.record("FLUSH_END topic=dist_sirt")
        self.ts.record("FLUSH_WAIT_START topic=dist_sirt")
        f.wait(timeout_ms=-1)
        self.ts.record("FLUSH_WAIT_END topic=dist_sirt")
        self.seq += 1
        # fin_buffers (and its bytearrays) released here on the main
        # thread with the GIL held — safe; the producer ULT has already
        # dropped its C++ refs during flush.
        return 0

    def finalize(self):
        del self.driver
