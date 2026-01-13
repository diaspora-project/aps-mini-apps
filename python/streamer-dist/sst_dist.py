import numpy as np
import json
import adios2.bindings as adios2  # if this fails, try: import adios2 as adios2
import time
import threading

def assign_data(comm_rank: int, comm_size: int, tot_sino: int, tot_cols: int) -> dict:
    """
    Helper to compute which sinograms a given rank/thread should process.
    Kept as-is in case other parts of your code still use it.
    """
    nsino = tot_sino // comm_size
    remaining = tot_sino % comm_size

    r = 1 if comm_rank < remaining else 0
    my_nsino = r + nsino
    beg_sino = (
        (1 + nsino) * comm_rank
        if comm_rank < remaining
        else (1 + nsino) * remaining + nsino * (comm_rank - remaining)
    )

    return {
        "Type": "MSG_DATAINFO_REQ",
        "tn_sinograms": tot_sino,
        "beg_sinogram": beg_sino,
        "n_sinograms": my_nsino,
        "n_rays_per_proj_row": tot_cols,
    }


class SSTDist:
    """
    Scatter-style distributor using ADIOS2 SST.

    - num_sinograms:   number of consumers (threads/ranks) on the C++ side
    - chunk_size:  number of float32 elements per consumer per step
    => total_size = num_sinograms * chunk_size

    Each push_image() call:
      - sends one SST step
      - 'data' is flattened into a 1D array of length total_size
      - metadata for each task is JSON-packed into meta_bytes + meta_offsets
    """

    def __init__(self, num_sinograms: int, chunk_size: int,
                 stream_name: str = "sirt_stream",
                 max_meta_bytes: int = 65536):
        self.num_sinograms = int(num_sinograms)
        self.chunk_size = int(chunk_size)
        self.total_size = self.num_sinograms * self.chunk_size
        self.max_meta_bytes = int(max_meta_bytes)

        self._lock = threading.Lock()
        self._closed = False
        self._last_push_ts = time.time()
        self._stream_name = stream_name

        print(f"Initializing SSTDist: num_sinograms={self.num_sinograms}, "
              f"chunk_size={self.chunk_size}, total_size={self.total_size}, "
              f"max_meta_bytes={self.max_meta_bytes}")

        # ADIOS2 SST setup
        self.ad = adios2.ADIOS()
        self.io = self.ad.DeclareIO("SST_sirt_IO")
        self.io.SetEngine("SST")
        self.io.SetParameters({
            "OpenTimeoutSecs": "5",
            "QueueLimit": "4",
            "QueueFullPolicy": "Discard",
        })

        # print("SSTDist: setting parameters...")
        # self.io.SetParameters({
        #     "QueueFullPolicy": "Discard",
        #     "QueueLimit": "4"
        # })

        print("SSTDist: defining variables...")

        # 1) Data variable: all chunks concatenated in one 1D array
        self.var_data = self.io.DefineVariable(
            "data",
            np.zeros(self.total_size, dtype=np.float32),
            [self.total_size],  # global shape
            [0],                # start
            [self.total_size],  # count
        )

        # 2) meta_bytes: concatenated JSON for all tasks (variable-length per step,
        #    but bounded by max_meta_bytes)
        self.var_meta_bytes = self.io.DefineVariable(
            "meta_bytes",
            np.zeros(self.max_meta_bytes, dtype=np.uint8),
            [self.max_meta_bytes],
            [0],
            [self.max_meta_bytes],
        )

        # 3) meta_offsets: prefix-sum offsets into meta_bytes, length = num_sinograms + 1
        self.var_meta_offsets = self.io.DefineVariable(
            "meta_offsets",
            np.zeros(self.num_sinograms + 1, dtype=np.int64),
            [self.num_sinograms + 1],
            [0],
            [self.num_sinograms + 1],
        )

        self.var_ctrl = self.io.DefineVariable(
            "ctrl",
            np.array([0], dtype=np.int32),
            [1], [0], [1]
        )

        print("SSTDist: opening SST writer...")

        # Open SST writer once
        self.writer = self.io.Open(stream_name, adios2.Mode.Write)

        print("SSTDist: initialized.")
    
    def _put_meta_bytes_prefix(self, meta_bytes: np.ndarray, total_meta_len: int):
        n = int(total_meta_len)
        if n <= 0:
            # send 1 byte of zero to avoid 0-length puts
            one = np.zeros(1, dtype=np.uint8)
            self.var_meta_bytes.SetSelection([[0], [1]])
            self.writer.Put(self.var_meta_bytes, one, adios2.Mode.Deferred)
            return

        self.var_meta_bytes.SetSelection([[0], [n]])
        self.writer.Put(self.var_meta_bytes, meta_bytes[:n], adios2.Mode.Deferred)

    def done_image(self):
        """
        Send a FIN message to indicate no more images will be sent.
        """
        print("Sending SST FIN message...")

        chunk_jsons = []
        for task_id in range(self.num_sinograms):
            meta = {
                "Type": "FIN",
            }
            chunk_jsons.append(json.dumps(meta))

        encoded = [m.encode("utf-8") for m in chunk_jsons]
        offsets = [0]
        for b in encoded:
            offsets.append(offsets[-1] + len(b))

        total_meta_len = offsets[-1]
        if total_meta_len > self.max_meta_bytes:
            raise ValueError(
                f"push_image: metadata bytes {total_meta_len} exceed max_meta_bytes "
                f"{self.max_meta_bytes}; increase max_meta_bytes."
            )

        # Prepare meta_bytes and meta_offsets for FIN message
        meta_bytes = np.zeros(self.max_meta_bytes, dtype=np.uint8)
        pos = 0
        for b in encoded:
            length = len(b)
            meta_bytes[pos:pos + length] = np.frombuffer(b, dtype=np.uint8)
            pos += length

        meta_offsets = np.array(offsets, dtype=np.int64)

        # Push FIN step
        with self._lock:
            if self.writer is None or self._closed:
                return
            began = False
            zeros = np.zeros(self.total_size, dtype=np.float32)
            try:
                self.writer.BeginStep(); began = True
                self.var_data.SetSelection([[0], [self.total_size]])
                self.writer.Put(self.var_data, zeros, adios2.Mode.Deferred)
                self.var_ctrl.SetSelection([[0], [1]])
                self.writer.Put(self.var_ctrl, np.array([1], dtype=np.int32), adios2.Mode.Deferred)
                
                self._put_meta_bytes_prefix(meta_bytes, total_meta_len)
                # offsets is fixed-length; send full
                self.var_meta_offsets.SetSelection([[0], [self.num_sinograms + 1]])
                self.writer.Put(self.var_meta_offsets, meta_offsets, adios2.Mode.Deferred)

                self.writer.PerformPuts()
                self.writer.EndStep()
            except Exception as e:
                if began:
                    try: self.writer.EndStep()
                    except Exception: pass
                print(f"SSTDist: exception during done_image: {e}")
                self._safe_reopen_writer()
                raise

        print("SST FIN message sent.")

    def push_image(
        self,
        data: np.ndarray,
        sequence_id: int,
        row: int,
        col: int,
        theta: float,
        projection_id: int,
        center: float,
    ) -> None:
        """
        Send one SST step.

        - data:       array of length num_sinograms * chunk_size
                      (will be flattened and cast to float32)
        - sequence_id, row, col, theta, projection_id, center:
                      used to build per-task JSON metadata.
        """

        print("Preparing data for SST push_image...")

        # Flatten and cast to float32
        flat = np.asarray(data, dtype=np.float32).ravel()
        if flat.size != self.total_size:
            raise ValueError(
                f"push_image: data has {flat.size} elements, expected {self.total_size} "
                f"(num_sinograms={self.num_sinograms} * chunk_size={self.chunk_size})"
            )

        dims = [int(row), int(col)]
        center_val = (dims[1] / 2.0) if center == 0.0 else float(center)

        # Build per-task JSON metadata
        chunk_jsons = []
        for task_id in range(self.num_sinograms):
            offset = task_id * self.chunk_size
            meta = {
                "Type": "MSG_DATA_REP",
                "task_id": int(task_id),
                "seq_n": int(sequence_id),
                "projection_id": int(projection_id),
                "theta": float(theta),
                "center": float(center_val),
                "dtype": "float32",
                "row": dims[0],
                "col": dims[1],
                "offset": int(offset),
                "length": int(self.chunk_size),
            }
            chunk_jsons.append(json.dumps(meta))

        print("Packing metadata for SST push_image...")

        # Pack metadata into a single byte buffer + offsets
        encoded = [m.encode("utf-8") for m in chunk_jsons]
        offsets = [0]
        for b in encoded:
            offsets.append(offsets[-1] + len(b))

        total_meta_len = offsets[-1]
        if total_meta_len > self.max_meta_bytes:
            raise ValueError(
                f"push_image: metadata bytes {total_meta_len} exceed max_meta_bytes "
                f"{self.max_meta_bytes}; increase max_meta_bytes."
            )

        # meta_bytes is fully sized, but only prefix [0:total_meta_len] is used
        meta_bytes = np.zeros(self.max_meta_bytes, dtype=np.uint8)
        pos = 0
        for b in encoded:
            length = len(b)
            meta_bytes[pos:pos + length] = np.frombuffer(b, dtype=np.uint8)
            pos += length

        meta_offsets = np.array(offsets, dtype=np.int64)

        print(f"Pushing SST step: sequence_id={sequence_id}, projection_id={projection_id}, "
              f"theta={theta}, center={center_val}")

        with self._lock:
            if self.writer is None or self._closed:
                raise RuntimeError("SSTDist writer is closed/unavailable")

            began = False
            try:
                self.writer.BeginStep()
                began = True

                self.writer.Put(self.var_ctrl, np.array([1], dtype=np.int32), adios2.Mode.Deferred)

                # data (fixed length)
                self.var_data.SetSelection([[0], [self.total_size]])
                self.writer.Put(self.var_data, flat, adios2.Mode.Deferred)

                # meta (prefix only)
                self._put_meta_bytes_prefix(meta_bytes, total_meta_len)

                # offsets (fixed length)
                self.var_meta_offsets.SetSelection([[0], [self.num_sinograms + 1]])
                self.writer.Put(self.var_meta_offsets, meta_offsets, adios2.Mode.Deferred)

                self.writer.PerformPuts()
                self.writer.EndStep()
                self._last_push_ts = time.time()

            except Exception as e:
                # best-effort close step if begun
                if began:
                    try:
                        self.writer.EndStep()
                    except Exception:
                        pass
                print(f"SSTDist: exception during push_image: {e}")
                self._safe_reopen_writer()
                raise

        print("SST step pushed.")

    def send_keepalive_once(self, tick_value: int):
        zeros = np.zeros(self.total_size, dtype=np.float32)
        meta_offsets = np.zeros(self.num_sinograms + 1, dtype=np.int64)
        tick = np.array([tick_value], dtype=np.int32)

        with self._lock:
            if self.writer is None or self._closed:
                return
            began = False
            try:
                self.writer.BeginStep()
                began = True
                self.writer.Put(self.var_ctrl, tick, adios2.Mode.Deferred)

                self.var_data.SetSelection([[0], [self.total_size]])
                self.writer.Put(self.var_data, zeros, adios2.Mode.Deferred)

                # send 0-byte meta prefix
                self._put_meta_bytes_prefix(np.empty(0, dtype=np.uint8), 0)

                self.var_meta_offsets.SetSelection([[0], [self.num_sinograms + 1]])
                self.writer.Put(self.var_meta_offsets, meta_offsets, adios2.Mode.Deferred)

                self.writer.PerformPuts()
                self.writer.EndStep()
            except Exception:
                if began:
                    try: self.writer.EndStep()
                    except Exception: pass
                self._safe_reopen_writer()
                raise

    def keepalive(self, period_sec=0.5):
        i = 0
        while not self._closed:
            try:
                self.send_keepalive_once(i)
                i += 1
            except Exception as e:
                print(f"keepalive: send failed: {e}")
                self._safe_reopen_writer()
            time.sleep(period_sec)


    def _safe_reopen_writer(self):
        # Best-effort: close and reopen so the writer doesn’t remain stuck/broken.
        try:
            if self.writer is not None:
                self.writer.Close()
        except Exception:
            pass
        try:
            # Recreate IO (cleaner than reusing a possibly corrupted Engine)
            self.io = self.ad.DeclareIO("SST_sirt_IO_reopen_" + str(int(time.time()*1000)))
            self.io.SetEngine("SST")
            self.io.SetParameters({
                "OpenTimeoutSecs": "5",
                "QueueLimit": "4",
                "QueueFullPolicy": "Discard",
            })
            # IMPORTANT: redefine variables on the new IO (same definitions)
            self.var_data = self.io.DefineVariable("data", np.zeros(self.total_size, np.float32),
                                                   [self.total_size], [0], [self.total_size])
            self.var_meta_bytes = self.io.DefineVariable("meta_bytes", np.zeros(self.max_meta_bytes, np.uint8),
                                                         [self.max_meta_bytes], [0], [self.max_meta_bytes])
            self.var_meta_offsets = self.io.DefineVariable("meta_offsets", np.zeros(self.num_sinograms + 1, np.int64),
                                                           [self.num_sinograms + 1], [0], [self.num_sinograms + 1])
            self.var_ctrl = self.io.DefineVariable("ctrl", np.array([0], np.int32), [1], [0], [1])

            self.writer = self.io.Open(self._stream_name, adios2.Mode.Write)
        except Exception:
            # If reopen fails, avoid spinning; caller can decide what to do
            self.writer = None

    def is_closed(self) -> bool:
        return self._closed

    def close(self) -> None:
        self._closed = True  # stop keepalive loop first
        w = getattr(self, "writer", None)
        self.writer = None
        if w is not None:
            try:
                w.Close()
            except Exception:
                pass
