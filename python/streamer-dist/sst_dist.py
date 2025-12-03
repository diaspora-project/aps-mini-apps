import numpy as np
import json
import adios2.bindings as adios2  # if this fails, try: import adios2 as adios2


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

        print(f"Initializing SSTDist: num_sinograms={self.num_sinograms}, "
              f"chunk_size={self.chunk_size}, total_size={self.total_size}, "
              f"max_meta_bytes={self.max_meta_bytes}")

        # ADIOS2 SST setup
        self.ad = adios2.ADIOS()
        self.io = self.ad.DeclareIO("SST_sirt_IO")
        self.io.SetEngine("SST")

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

        print("SSTDist: opening SST writer...")

        # Open SST writer once
        self.writer = self.io.Open(stream_name, adios2.Mode.Write)

        print("SSTDist: initialized.")

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

        # ---- Atomic SST step: data + metadata together ----
        self.writer.BeginStep()
        self.writer.Put(self.var_data, flat)
        self.writer.Put(self.var_meta_bytes, meta_bytes)
        self.writer.Put(self.var_meta_offsets, meta_offsets)
        self.writer.EndStep()

    def close(self) -> None:
        """Close the SST writer when done."""
        if hasattr(self, "writer") and self.writer is not None:
            self.writer.Close()
            self.writer = None
