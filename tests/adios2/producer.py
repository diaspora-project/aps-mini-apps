# producer_scatter_json.py
import time
import json
import numpy as np
import adios2.bindings as adios2   # or `import adios2 as adios2` if that works

stream_name   = "sirt_stream"
num_chunks    = 4         # number of consumers
chunk_size    = 8         # data elements per consumer
total_size    = num_chunks * chunk_size

ad = adios2.ADIOS()
io = ad.DeclareIO("ProducerIO")
io.SetEngine("SST")

# Variables:
# 1) data: all chunks concatenated
var_data = io.DefineVariable(
    "data",
    np.zeros(total_size, dtype=np.float64),
    [total_size], [0], [total_size],
)

# 2) meta_bytes: concatenated JSON bytes (length varies per step)
#    We define with a max size, but will only write the prefix we use.
max_meta_bytes = 4096  # adjust as needed
var_meta_bytes = io.DefineVariable(
    "meta_bytes",
    np.zeros(max_meta_bytes, dtype=np.uint8),
    [max_meta_bytes], [0], [max_meta_bytes],
)

# 3) meta_offsets: prefix-sum offsets into meta_bytes, length = num_chunks+1
var_meta_offsets = io.DefineVariable(
    "meta_offsets",
    np.zeros(num_chunks + 1, dtype=np.int64),
    [num_chunks + 1], [0], [num_chunks + 1],
)

writer = io.Open(stream_name, adios2.Mode.Write)

for step in range(5):
    # ---- build scatter data ----
    data = np.zeros(total_size, dtype=np.float64)
    chunk_json_list = []

    for c in range(num_chunks):
        start = c * chunk_size
        end   = start + chunk_size

        # Example payload
        for i in range(chunk_size):
            data[start + i] = step * 1000.0 + c * 100.0 + i

        # Example metadata for this chunk
        meta = {
            "step": step,
            "chunk_id": c,
            "offset": int(start),
            "length": int(chunk_size),
            "note": f"chunk {c} at step {step}",
        }
        chunk_json_list.append(json.dumps(meta))

    # ---- pack JSONs into a single byte buffer + offsets ----
    encoded_list = [s.encode("utf-8") for s in chunk_json_list]
    offsets = [0]
    for b in encoded_list:
        offsets.append(offsets[-1] + len(b))

    total_meta_len = offsets[-1]
    if total_meta_len > max_meta_bytes:
        raise RuntimeError("Increase max_meta_bytes; metadata too large")

    meta_bytes = np.zeros(max_meta_bytes, dtype=np.uint8)
    pos = 0
    for b in encoded_list:
        meta_bytes[pos:pos+len(b)] = np.frombuffer(b, dtype=np.uint8)
        pos += len(b)

    meta_offsets = np.array(offsets, dtype=np.int64)

    # ---- atomic write in one step ----
    writer.BeginStep()
    writer.Put(var_data, data)
    writer.Put(var_meta_bytes, meta_bytes)
    writer.Put(var_meta_offsets, meta_offsets)
    writer.EndStep()

    print(f"[Producer] step={step}, first chunk data={data[0:chunk_size]}")
    time.sleep(0.5)

writer.Close()
print("[Producer] Done.")
