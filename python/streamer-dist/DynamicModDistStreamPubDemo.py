import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '../common'))
sys.path.append(os.path.join(os.path.dirname(__file__), '../common/local'))
import argparse
import numpy as np
import time
import math
import TraceSerializer
import tomopy as tp
import json
from mofka_dist import MofkaDist
from sst_dist import SSTDist
import csv
import signal
#from memory_profiler import profile
import threading
import multiprocessing
from multiprocessing import shared_memory
from dataclasses import dataclass
from collections import deque
import traceback
import uuid

def parse_arguments():
  parser = argparse.ArgumentParser( description='Data Distributor Process')
  parser.add_argument('--protocol', default="na+sm", help='Mofka protocol')

  parser.add_argument('--dynamic_loadbalancing', default="true", help='Enable dynamic load balancing')

  parser.add_argument('--group_file', type=str, default="mofka.json",
                      help='Group file for the mofka server')

  parser.add_argument('--batchsize', type=int, default=16,
                      help='mofka batch size')

  parser.add_argument('--ntask_sirt', type=int, default=0,
                      help='number of reconstruction tasks')
  # TQ communication
  parser.add_argument('--beg_sinogram', type=int,
                          help='Starting sinogram for reconstruction')
  parser.add_argument('--num_sinograms', type=int,
                          help='Number of sinograms to reconstruct (rows)')
  parser.add_argument('--num_columns', type=int,
                          help='Number of columns (cols)')

  # Available pre-processing options
  parser.add_argument('--degree_to_radian', action='store_true', default=False,
              help='Converts rotation information to radian.')
  parser.add_argument('--mlog', action='store_true', default=False,
              help='Takes the minus log of projection data (projection data is divided by 50000 also).')
  parser.add_argument('--uint16_to_float32', action='store_true', default=False,
              help='Converts uint16 image byte sequence to float32.')
  parser.add_argument('--uint8_to_float32', action='store_true', default=False,
              help='Converts uint8 image byte sequence to float32.')
  parser.add_argument('--cast_to_float32', action='store_true', default=False,
              help='Casts incoming image byte sequence to float32.')
  parser.add_argument('--normalize', action='store_true', default=False,
              help='Normalizes incoming projection data with previously received dark and flat field.')
  parser.add_argument('--remove_invalids', action='store_true', default=False,
              help='Removes invalid measurements from incoming projections, i.e. negatives, nans and infs.')
  parser.add_argument('--remove_stripes', action='store_true', default=False,
              help='Removes stripes using fourier-wavelet method (in tomopy).')

  # Enable/disable steps
  parser.add_argument('--skip_serialize', action='store_true', default=False,
              help='Disable deserialization of the incoming messages. Only receives data from remote data source, since deserialization is required for other operations.')
  parser.add_argument('--check_seq', action='store_true', default=False,
              help='Checks the incoming packages sequence numbers and prints out when there is problem (does not terminate). Expected sequence is 0, 1, .., i, i+1, ...')

  parser.add_argument('--logdir', type=str, default='.',
              help='Path to save log files.')
  
  parser.add_argument('--sst', type=bool, default=False,
              help='Use SST for data distribution.')


  return parser.parse_args()

import queue
# mofka_queue = queue.Queue()

@dataclass
class ImageDesc:
    slot: int
    nbytes: int
    msg_id: str
    sequence_id: int
    num_sinograms: int
    num_columns: int
    rotation: float
    unique_id: int
    center: float
    attempts: int = 0
    last_attempt_ts: float = 0.0

class ImagePacket:
  def __init__(self, data: np.ndarray, sequence_id: int, num_sinograms: int, num_columns: int,
                    rotation: float, unique_id: int, center: float, msg_id: str, timeout: float = 0.2):
    self.data = data
    self.sequence_id = sequence_id
    self.num_sinograms = num_sinograms
    self.num_columns = num_columns
    self.rotation = rotation
    self.unique_id = unique_id
    self.center = center
    self.msg_id = msg_id
    self.timeout = timeout

from typing import Any, Optional
class SharedRing:
  # def __init__(self, num_slots: int, slot_bytes: int, shm_name: Optional[str] = None, create: bool = True):
  #   self.num_slots = num_slots
  #   self.slot_bytes = slot_bytes
  #   self.total_bytes = num_slots * slot_bytes

  #   if create:
  #     self.shm = shared_memory.SharedMemory(create=True, size=self.total_bytes)
  #     self.name = self.shm.name
  #     self._owner = True
  #   else:
  #     assert shm_name is not None
  #     self.shm = shared_memory.SharedMemory(name=shm_name, create=False)
  #     self.name = shm_name
  #     self._owner = False
  def __init__(self, num_slots: int, slot_bytes: int, shm_name: Optional[str] = None, create: bool = True):
    self.num_slots = num_slots
    self.slot_bytes = slot_bytes
    self.total_bytes = num_slots * slot_bytes

    # normalize shm name (no leading slash)
    norm_name = None
    if shm_name is not None:
      norm_name = shm_name.lstrip("/")

    if create:
      # If you want deterministic naming, you can pass name=norm_name when norm_name is not None
      self.shm = shared_memory.SharedMemory(create=True, size=self.total_bytes, name=norm_name)
      self._owner = True
    else:
      assert norm_name is not None
      self.shm = shared_memory.SharedMemory(name=norm_name, create=False)
      self._owner = False

    # Always store normalized name
    self.name = self.shm.name.lstrip("/")

    # self.buf = self.shm.buf  # memoryview

  def slot_view(self, slot: int) -> memoryview:
    off = slot * self.slot_bytes
    return self.shm.buf[off : off + self.slot_bytes]

  def close(self):
    # # Release our own exported buffer view first
    # try:
    #   if hasattr(self, "buf") and self.buf is not None:
    #     self.buf.release()
    #     self.buf = None
    # except Exception:
    #   pass
    self.shm.close()

  def unlink(self):
    # only owner should unlink
    if self._owner:
        self.shm.unlink()

def flush_mofka_producer(args, shm_name, num_slots, slot_bytes, desc_q, ack_q):
    ring = SharedRing(num_slots=num_slots, slot_bytes=slot_bytes, shm_name=shm_name, create=False)

    md = MofkaDist(group_file=args.group_file, batchsize=args.batchsize)
    md.handshake(args.ntask_sirt, args.num_sinograms, args.num_columns)
    p = md.producer(topic_name="dist_sirt", producer_name="dist")

    try:
      while True:
        desc = desc_q.get()
        if desc is None:
          break
        
        if desc.sequence_id < 0:
          print(f"Received complete signal in mofka producer with sequence_id={desc.sequence_id}. Exiting ...")
          break
        else:
          try:
            mv = ring.slot_view(desc.slot)[:desc.nbytes]
            n_f32 = desc.nbytes // 4
            payload = np.frombuffer(mv, dtype=np.float32, count=n_f32).copy()
            # payload = np.frombuffer(mv, dtype=np.float32, count=n_f32)
            md.push_image(payload,
                          desc.sequence_id,
                          desc.num_sinograms,
                          desc.num_columns,
                          desc.rotation,
                          desc.unique_id,
                          desc.center,
                          producer=p)
            ack_q.put(("OK", desc.slot, desc.msg_id, None))
          except Exception as e:
            ack_q.put(("EXC", desc.slot, desc.msg_id, repr(e)))
          finally:
            try:
              del payload
              mv.release()
              del mv
            except Exception:
              pass
            mv = None
    finally:
      try:
        md.last_flush(producer=p)
      except Exception:
        pass
      try:
        md.done_image(producer=p)
      except Exception:
        pass
      # force cleanup before shm close (helps some native libs)
      try:
        del p
        del md
      except Exception:
        pass
      ring.close()

  # while True:
  #   try:
  #     image_packet = mofka_queue.get(timeout=0.01)
  #     print(f"Sending image seq_id {image_packet.sequence_id} to sirt through Mofka")
  #     md.push_image(image_packet.data, image_packet.sequence_id, image_packet.num_sinograms, image_packet.num_columns,
  #                   image_packet.rotation, image_packet.unique_id, image_packet.center,
  #                   producer=p)
  #   except queue.Empty:
  #     continue
  # print("Exiting mofka producer flush thread ...")

class ShmSender:
  def __init__(self, name, num_slots, slot_bytes, init_args, flush_producer, ctx=None):
    self.ctx = ctx or multiprocessing.get_context("spawn")
    self.init_args = init_args

    self.uncertain_ids = set()

    self.ring = SharedRing(num_slots=num_slots, slot_bytes=slot_bytes, create=True)
    self.desc_q = self.ctx.Queue(maxsize=num_slots * 4)
    self.ack_q  = self.ctx.Queue(maxsize=num_slots * 4)

    self.free_slots = list(range(num_slots))

    # msg_id -> desc
    self.inflight = {}
    self.pending_definite = deque()   # safe retries
    self.pending_uncertain = deque()  # avoid resending

    self.flush_producer = flush_producer
    self.name = name

    self.last_ok_or_exc_time = time.time()
    self.proc = self._start_proc()

    # knobs
    self.MAX_RETRIES_DEFINITE = 1
    self.UNCERTAIN_RETRY_DELAY_SEC = 300
    self.UNCERTAIN_MAX_RETRIES = 1
    self.RESTART_ON_STALL_SEC = 10

    self.async_waiting_queue = queue.Queue()

  def _start_proc(self):
    p = self.ctx.Process(
      target=self.flush_producer,
      args=(self.init_args, self.ring.name, self.ring.num_slots, self.ring.slot_bytes,
            self.desc_q, self.ack_q),
      daemon=False
    )
    p.start()
    return p

  def _json_safe(self, x):
    if isinstance(x, set):
        return sorted(list(x))
    try:
        import numpy as np
        if isinstance(x, (np.integer, np.floating)):
            return x.item()
        if isinstance(x, np.ndarray):
            return f"<ndarray shape={x.shape} dtype={x.dtype}>"
    except Exception:
        pass
    return str(x)

  def _drain_acks(self, max_items=256):
    for _ in range(max_items):
      try:
        status, slot, msg_id, err = self.ack_q.get_nowait()
      except queue.Empty:
        break

      self.last_ok_or_exc_time = time.time()

      desc = self.inflight.get(msg_id)
      # If it isn't inflight anymore, ignore (late ack)
      if desc is None:
        continue

      if status == "OK":
        # success: free slot
        del self.inflight[msg_id]
        self.free_slots.append(slot)

      elif status == "EXC":
        # definite failure: safe to retry up to MAX_RETRIES_DEFINITE
        desc.attempts += 1
        desc.last_attempt_ts = time.time()

        if desc.attempts <= self.MAX_RETRIES_DEFINITE:
          self.pending_definite.append(desc)
        else:
          # Give up: remove inflight
          del self.inflight[msg_id]

          # Move to uncertain retry bucket (separate counter)
          desc.attempts = 0
          desc.last_attempt_ts = time.time()

          spilled = self._spill_failed_payload(desc, err or "unknown")
          if spilled:
            # We spilled successfully => free slot; DO NOT keep for retry
            self.free_slots.append(slot)
            sv = None
            try:
              sv = self.ring.slot_view(slot)
              sv[:] = b"\x00" * len(sv)
            finally:
              try:
                if sv is not None:
                  sv.release()
              except Exception:
                pass
          else:
            # Spill failed => keep parked (slot remains occupied)
            self.pending_uncertain.append(desc)
            self.uncertain_ids.add(msg_id)

  def _spill_failed_payload(self, desc: ImageDesc, err: str):
    try:
      outdir = os.path.join(getattr(self.init_args, "logdir", "."), "failed_payloads")
      os.makedirs(outdir, exist_ok=True)
      path = os.path.join(outdir, f"{desc.msg_id}.bin")

      mv = None
      try:
        mv = self.ring.slot_view(desc.slot)[:desc.nbytes]
        with open(path, "wb") as f:
          f.write(mv.tobytes())
      finally:
        try:
          if mv is not None:
            mv.release()
        except Exception:
          pass

      meta_path = os.path.join(outdir, f"{self.name}-{desc.msg_id}.json")
      with open(meta_path, "w") as f:
        meta = {
          "msg_id": desc.msg_id,
          "sequence_id": int(desc.sequence_id),
          "slot": int(desc.slot),
          "nbytes": int(desc.nbytes),
          "num_sinograms": int(desc.num_sinograms),
          "num_columns": int(desc.num_columns),
          "rotation": float(desc.rotation),
          "unique_id": int(desc.unique_id),
          "center": float(desc.center),
          "attempts": int(desc.attempts),
          "error": str(err),
          "ts": time.time(),
        }
        json.dump(meta, f, indent=2, default=self._json_safe)

      print(f"[WARN] Spilled failed payload to {path} (slot {desc.slot}). Error: {err}")
      return True
    except Exception as e:
      print(f"[WARN] Failed to spill payload for {desc.msg_id}: {e}")
      return False

  def _try_send_desc(self, desc, timeout=0.0):
    try:
      self.desc_q.put(desc, timeout=timeout)
      return True
    except queue.Full:
      return False

  def _service_queues(self):
    self._drain_acks()

    # send definite retries first (safe)
    for _ in range(64):
      if not self.pending_definite:
        break
      d = self.pending_definite[0]
      if self._try_send_desc(d, timeout=0.0):
        self.pending_definite.popleft()
      else:
        break

    # extremely conservative resend of uncertain after delay
    now = time.time()
    max_checks = min(16, len(self.pending_uncertain))  # avoid infinite looping
    for _ in range(max_checks):
      if not self.pending_uncertain:
        break

      d = self.pending_uncertain[0]

      # If already maxed out, park it but don't block the queue.
      if d.attempts >= self.UNCERTAIN_MAX_RETRIES:
        self.pending_uncertain.rotate(-1)
        continue

      # If not yet time to retry, rotate it so others can be considered.
      if now - d.last_attempt_ts < self.UNCERTAIN_RETRY_DELAY_SEC:
        self.pending_uncertain.rotate(-1)
        continue

      # Try sending; on success pop it; on failure rotate and stop for now
      if self._try_send_desc(d, timeout=0.0):
        d.attempts += 1
        d.last_attempt_ts = now
        self.pending_uncertain.popleft()
      else:
        # queue full - don't spin; try later
        self.pending_uncertain.rotate(-1)
        break
  
  def send_fin(self, timeout=1.0) -> bool:
    # doesn't use shm slots, just sends a control desc
    fin = ImageDesc(
      slot=0, nbytes=0,
      msg_id=f"{int(time.time())}:FIN",
      sequence_id=-1,
      num_sinograms=0, num_columns=0,
      rotation=0.0, unique_id=0, center=0.0,
      attempts=0, last_attempt_ts=time.time()
    )
    try:
      self.desc_q.put(fin, timeout=timeout)
      return True
    except queue.Full:
      return False
    
  def poll(self):
    self._service_queues()
    
  def wait_worker_exit(self, timeout=5.0) -> bool:
    self.proc.join(timeout=timeout)
    return not self.proc.is_alive()

  def enqueue_image(self, data: np.ndarray, sequence_id: int, num_sinograms: int, num_columns: int,
                    rotation: float, unique_id: int, center: float, msg_id: str,
                    timeout: float = 0.2) -> bool:
    """
    Returns False if no slot or worker dead.
    """
    self._service_queues()

    if not self.proc.is_alive():
      return False

    deadline = time.time() + timeout
    while not self.free_slots and time.time() < deadline:
      self._service_queues()
      time.sleep(0.001)

    if not self.free_slots:
      return False

    slot = self.free_slots.pop()
    # payload_mv = memoryview(data).cast('B')
    payload_mv = memoryview(data).cast('B')
    print(f"seq_id: {sequence_id} data size: {data.nbytes}, payload_mv size: {payload_mv.nbytes}, slot_bytes: {self.ring.slot_bytes}")

    if payload_mv.nbytes > self.ring.slot_bytes:
      self.free_slots.append(slot)
      raise ValueError(f"payload too big: {payload_mv.nbytes} > {self.ring.slot_bytes}")

    # write to shm
    sv = None
    try:
      sv = self.ring.slot_view(slot)
      sv[:payload_mv.nbytes] = payload_mv
    finally:
      try:
        if sv is not None:
          sv.release()
      except Exception:
        pass

    desc = ImageDesc(slot=slot, nbytes=payload_mv.nbytes, msg_id=msg_id,
                      sequence_id=sequence_id, num_sinograms=num_sinograms, num_columns=num_columns,
                      rotation=rotation, unique_id=unique_id, center=center,
                      attempts=0, last_attempt_ts=time.time())

    self.inflight[msg_id] = desc

    # First send attempt
    if not self._try_send_desc(desc, timeout=timeout):
      # If queue is full, keep as definite pending (safe) because it never left process boundary
      self.pending_definite.append(desc)

    return True

  def async_enqueue_image(self, data: np.ndarray, sequence_id: int, num_sinograms: int, num_columns: int,
                    rotation: float, unique_id: int, center: float, msg_id: str,
                    timeout: float = 0.2) -> bool:
    self.async_waiting_queue.put(ImagePacket(
      data=data, sequence_id=sequence_id, num_sinograms=num_sinograms, num_columns=num_columns,
      rotation=rotation, unique_id=unique_id, center=center, msg_id=msg_id, timeout=timeout
    ))
    return True
  
  def _async_sender_thread_fn(self):
    print("Starting async sender thread ...")
    while True:
      try:
        img_msg = self.async_waiting_queue.get(timeout=0.1)
      except queue.Empty:
        img_msg = None

      # ALWAYS service + watchdog, even if idle
      self.poll()
      self.maybe_restart_if_stalled()

      if img_msg is None:
        continue

      print(f"Queueing image seq_id {img_msg.sequence_id} to sirt through {self.name}")
      ok = self.enqueue_image(
        data=img_msg.data,
        sequence_id=img_msg.sequence_id,
        num_sinograms=img_msg.num_sinograms,
        num_columns=img_msg.num_columns,
        rotation=img_msg.rotation,
        unique_id=img_msg.unique_id,
        center=img_msg.center,
        msg_id=img_msg.msg_id,
        timeout=img_msg.timeout,
      )
      if not ok:
        # optional: avoid tight loop if backpressure
        time.sleep(0.001)

  def start_async_sender_thread(self):
    threading.Thread(target=self._async_sender_thread_fn, daemon=True).start()

  def maybe_restart_if_stalled(self):
    """
    If the worker stopped producing OK/EXC acks for too long, assume it is hung.
    Restart it, but do NOT resend inflight immediately (minimize duplicates).
    """
    now = time.time()
    if now - self.last_ok_or_exc_time < self.RESTART_ON_STALL_SEC:
      return False

    # Stall: restart process
    if self.proc.is_alive():
      self.proc.terminate()
      self.proc.join(timeout=1.0)

    # Recreate queues (old ones may have stuck items)
    self.desc_q = self.ctx.Queue(maxsize=self.ring.num_slots * 4)
    self.ack_q  = self.ctx.Queue(maxsize=self.ring.num_slots * 4)
    self.proc = self._start_proc()

    # owned_slots = {d.slot for d in self.inflight.values()}

    # Move all inflight to UNCERTAIN backlog. Do NOT resend now.
    # Keep their slots occupied so payloads are preserved in shm.
    for msg_id, desc in self.inflight.items():
      # if desc.slot not in owned_slots:
      #   continue
      if msg_id not in self.uncertain_ids:
          desc.last_attempt_ts = now
          self.pending_uncertain.append(desc)
          self.uncertain_ids.add(msg_id)

    self.last_ok_or_exc_time = now
    return True

  def stop(self, force_kill_after=2.0):
    try:
      self.desc_q.put(None, timeout=0.2)
    except Exception:
      pass
    self.proc.join(timeout=force_kill_after)
    if self.proc.is_alive():
      self.proc.terminate()
      self.proc.join(timeout=1.0)

    self.ring.close()
    self.ring.unlink()

# def restart_worker(sender: ShmSender, init_args):
#     # kill old proc
#     if sender.proc.is_alive():
#         sender.proc.terminate()
#         sender.proc.join(timeout=1.0)

#     # reset queues and free lists conservatively
#     sender.free_slots = list(range(sender.ring.num_slots))
#     sender.inflight.clear()

#     ctx = sender.ctx
#     sender.desc_q = ctx.Queue(maxsize=sender.ring.num_slots * 2)
#     sender.ack_q  = ctx.Queue(maxsize=sender.ring.num_slots * 2)

#     sender.proc = ctx.Process(
#         target=self.flush_producer,
#         args=(init_args, sender.ring.name, sender.ring.num_slots, sender.ring.slot_bytes,
#               sender.desc_q, sender.ack_q),
#         daemon=False
#     )
#     sender.proc.start()


def flush_sst_producer(args, shm_name, num_slots, slot_bytes, desc_q, ack_q):
    ring = SharedRing(num_slots=num_slots, slot_bytes=slot_bytes, shm_name=shm_name, create=False)

    contact_file = args.logdir + "/sirt_stream"
    print(f"Setting up SST for data distribution at {contact_file}...")
    sst_dist = SSTDist(num_sinograms=args.num_sinograms, chunk_size=args.num_columns, 
                            stream_name=contact_file, max_meta_bytes=65536)

    try:
      while True:
        desc = desc_q.get()
        if desc is None:
          break
        
        if desc.sequence_id < 0:
          print(f"Received complete signal in SST producer with sequence_id={desc.sequence_id}. Sending FIN message to sirt")
          sst_dist.done_image()
          print("Sending keepalive to SST stream ...")
          sst_dist.keepalive(period_sec=0.5)
        else:
          try:
            mv = ring.slot_view(desc.slot)[:desc.nbytes]
            n_f32 = desc.nbytes // 4
            payload = np.frombuffer(mv, dtype=np.float32, count=n_f32).copy()
            # payload = np.frombuffer(mv, dtype=np.float32, count=n_f32)

            sst_dist.push_image(payload,
                        desc.sequence_id,
                        desc.num_sinograms,
                        desc.num_columns,
                        desc.rotation,
                        desc.unique_id,
                        desc.center)

            ack_q.put(("OK", desc.slot, desc.msg_id, None))
          except Exception as e:
            ack_q.put(("EXC", desc.slot, desc.msg_id, repr(e)))
          finally:
            try:
              del payload
              mv.release()
              del mv
            except Exception:
              pass
            mv = None
    finally:
      # force cleanup before shm close (helps some native libs)
      try:
        if sst_dist is not None:
          sst_dist.close()
      except Exception:
          pass
      ring.close()
      print("Cleaning up SST stream ...")


def move_task(task_id, from_worker, to_worker, producer, action_seq, progress):
  stop_info = {
      "Type": "END_TASK",
      "task_id": task_id,
      "worker_id": from_worker,
      "progress": progress,
      "action_seq": action_seq
  }
  action_seq += 1
  producer.push(stop_info, bytearray(1), partition=0)
  print(f"[LB] --> action_seq={action_seq} Stopping task {task_id} on worker {from_worker} for reassignment")
  assign_info = {
      "Type": "START_TASK",
      "task_id": task_id,
      "worker_id": to_worker,
      "from_worker_id": from_worker,
      "progress": progress,
      "action_seq": action_seq
  }
  action_seq += 1
  producer.push(assign_info, bytearray(1), partition=0)
  print(f"[LB] --> action_seq={action_seq} Reassigning task {task_id} from worker {from_worker} to worker {to_worker}")
  producer.flush()
  return action_seq

def flush_action_producer(args, shm_name, num_slots, slot_bytes, desc_q, ack_q):
  ring = SharedRing(num_slots=num_slots, slot_bytes=slot_bytes, shm_name=shm_name, create=False)

  action_mofka_dist = MofkaDist(group_file=args.group_file, batchsize=args.batchsize)
  action_producer = action_mofka_dist.producer(topic_name="dist_sirt_action", producer_name="dist")

  try:
    while True:
      desc = desc_q.get()
      if desc is None:
        break

      try:
        producer.push(desc.assign_info, bytearray(1), partition=0)
        producer.flush()
        ack_q.put(("OK", desc.slot, desc.msg_id, None))
      except Exception as e:
        ack_q.put(("EXC", desc.slot, desc.msg_id, repr(e)))
  finally:
    try:
      del action_producer
      del action_mofka_dist
    except Exception:
      pass
    ring.close()

# def task_to_worker_assignment(action_producer, action_consumer, args, action_mofka_dist):
def task_to_worker_assignment(args, num_workers):

  action_mofka_dist = MofkaDist(group_file=args.group_file, batchsize=args.batchsize)
  action_consumer = action_mofka_dist.consumer(topic_name="sirt_dist_action", consumer_name="dist")

  action_producer_mofka_dist = MofkaDist(group_file=args.group_file, batchsize=args.batchsize)
  action_producer = action_producer_mofka_dist.producer(topic_name="dist_sirt_action", producer_name="dist")

  action_seq = 0

  print("Assigning tasks to workers ...")
  num_tasks = args.ntask_sirt
  # assign tasks to workers in round-robin fashion
  task_to_worker = {}
  worker_to_task = [set() for _ in range(num_workers)]
  for t in range(num_tasks):
    task_to_worker[t] = t % num_workers
    worker_to_task[t % num_workers].add(t)
  # for t in range(num_tasks):
  #   task_to_worker[t] = 0
  #   worker_to_task[0].append(t)
  # for t in range(num_tasks):
  #     task_to_worker[t] = t % (num_workers - 1) + 1
  #     worker_to_task[t % (num_workers - 1) + 1].append(t)

  for w in range(num_workers):
    print(f"Worker {w} assigned tasks: {worker_to_task[w]}")
    for t in worker_to_task[w]:
      assign_info = {
          "Type": "START_TASK",
          "worker_id": w,
          "task_id": t,
          "action_seq": action_seq
      }
      action_seq += 1
      action_producer.push(assign_info, bytearray(1), partition=0)
      print(f"Send info to sirt: {assign_info}")
  action_producer.flush()

  # time.sleep(100000000)

  task_progress = {}
  working_tasks = set(range(num_tasks))
  for task_id in working_tasks:
    task_progress[task_id] = 0
  worker_progress = np.zeros(num_workers)

  total_progress = 0

  # Listen from consumer and take actions if needed
  if args.dynamic_loadbalancing.lower() == "true":
    print("Dynamic load balancing is enabled")
  else:
    print("Dynamic load balancing is disabled")
  
  round = 0

  progress_threshold_window = len(working_tasks) * 16 * 3
  progress_threshold = progress_threshold_window

  worker_active_periods = np.zeros(num_workers)

  last_worst_gap = 0.8
  
  while args.dynamic_loadbalancing.lower() == "true":
    print(f"[LB] Load balancing round {round}: Collecting data from recon tasks ----------- ")
    round += 1

    force_reassignment = False

    # time.sleep(1000000)

    try:
      f = action_consumer.pull()
      event = f.wait()
      metadata = json.loads(event.metadata)
      if metadata["Type"] == "PROGRESS":
        task_id = metadata["task_id"]
        worker_id = task_to_worker[task_id]
        progress = metadata["progress"]
        improved_progress = max(0, progress - task_progress[task_id])
        print(f"[LB] Received progress update: task {task_id} on worker {worker_id} progress {progress - improved_progress} --> {progress}")
        # task_progress[task_id] = progress
        task_progress[task_id] += improved_progress
        worker_progress[worker_id] += improved_progress
        total_progress += improved_progress
      elif metadata["Type"] == "FINISHED":
        task_id = metadata["task_id"]
        worker_id = metadata["worker_id"]
        working_tasks.discard(task_id)
        worker_to_task[worker_id].discard(task_id)
        print(f"[LB] Task-{task_id} in Worker-{worker_id} has finished")
        force_reassignment = True
      else:
        print(f"[LB] Unknown metadata received: {event.metadata},")
        continue
    except Exception as e:
      print(f"[LB] Exception while processing message: {metadata}: {e}")
      continue
    
    # Make reassignment only if total progress is large enough to reflect performance
    if not force_reassignment and total_progress < progress_threshold:
      print(f"[LB] Total progress {total_progress} is less than threshold {progress_threshold}, continue collecting ...")
      continue

    print(f"[LB] Total progress {total_progress} reached threshold {progress_threshold}, evaluating reassignment ...")
    progress_threshold = total_progress + progress_threshold_window

    # sorting task_id based on progress from smallest to largest
    sorted_tasks = sorted(working_tasks, key=lambda x: task_progress[x])
    min_progress = task_progress[sorted_tasks[0]]
    max_progress = task_progress[sorted_tasks[-1]]
    worst_gap = min_progress / max_progress
    if force_reassignment:
      print(f"[LB] Enforce task assignment due to task/worker structural changes")
    elif worst_gap > last_worst_gap:
      print(f"[LB] Progress between tasks are improving: Old: {last_worst_gap:.4f} --> New : {worst_gap:.4f} no need for adjustment")
      last_worst_gap = worst_gap
      continue
    else:
      print(f"[LB] Progress between tasks getting worse: Old: {last_worst_gap:.4f} --> New : {worst_gap:.4f} need for adjustment")
    last_worst_gap = worst_gap
    # How task are lagging behind the fastest task
    task_lag = {}
    sum_lag = 0
    for t in working_tasks:
      task_lag[t] = (max_progress - task_progress[t])/(max_progress - min_progress)
      sum_lag += task_lag[t]
    # # weights are normalized lags
    # task_weights = {}
    # for t in working_tasks:
    #   task_weights[t] = task_lag[t]*len(working_tasks)/sum_lag
    task_weights = np.ones(num_tasks)
    
    # worker_progress = [0 for _ in range(num_workers)]
    # for i in range(num_tasks):
    #   worker_progress[task_to_worker[i]] += task_progress[i]
    # sum_task_progress = sum(task_progress)
    # for i in range(num_workers):
    #   # print("[LB]: wp: ", worker_progress[i], worker_to_task)
    #   if worker_progress[i] > 0:
    #     worker_progress[i] /= len(worker_to_task[i])
    #   else:
    #     worker_progress[i] = sum_task_progress / num_workers  

    # sum_worker_progress = sum(worker_progress)
    # for i in range(num_workers):
    #   worker_progress[i] *= sum_task_progress / sum_worker_progress
    # # worker capacities are normalized progress
    # worker_caps = [worker_progress[i]*num_tasks/sum_worker_progress for i in range(num_workers)]
    avg_cap = num_tasks / num_workers
    for i in range(num_workers):
      if len(worker_to_task) > 0:
        worker_active_periods[i] += 1
    worker_speeds = worker_progress / worker_active_periods
    avg_speed = np.sum(worker_progress) / np.max(worker_active_periods) / num_workers
    worker_caps = worker_speeds * avg_cap / avg_speed
    
    
    # reassign tasks based on their weights and workers' capacities
    worker_weights = [sum(task_weights[t] for t in worker_to_task[w]) for w in range(num_workers)]
    # surplus capacities are the extra capacities after considering assigned task weights
    # positive surplus means the worker can take more tasks
    # negative surplus means the worker is overloaded
    # task reassignment is to move tasks from workers with negative surplus to workers with positive surplus
    # optimal assignment is when all workers have zero surplus capacity 
    surplus_worker_caps = np.array(worker_caps) - np.array(worker_weights)
    sorted_surplus_worker_caps = sorted(range(num_workers), key=lambda x: surplus_worker_caps[x])

    print(f"[LB] Progress report:")
    print(f"[LB] min_task_progress: Task-{sorted_tasks[0]} (progress={min_progress}), max_task_progress: Task-{sorted_tasks[-1]} (progress={max_progress})")
    print(f"[LB] sorted task progress: {sorted_tasks}")
    print(f"[LB] avg_cap={avg_cap:.2f} --> avg_speed={avg_speed:.2f}")
    print(f"[LB] sorted surplus worker caps: {sorted_surplus_worker_caps}")
    for w in range(num_workers):
      print(f"[LB]  Worker {w}: progress={worker_progress[w]}, speed={worker_speeds[w]:.2f} cap={worker_caps[w]:.2f}, weight={worker_weights[w]:.2f}, surplus={surplus_worker_caps[w]:.2f}")
      for t in worker_to_task[w]:
        print(f"[LB]    --> Task {t}: progress={task_progress[t]}, lag={task_lag[t]:.2f}, weight={task_weights[t]:.2f}")

    task_assigned = False

    # Move min_progress task to the most spacious worker
    from_task = sorted_tasks[0]
    from_worker = task_to_worker[from_task]

    to_task = -1
    to_worker = -1

    target_worker = sorted_surplus_worker_caps[-1]
    if target_worker != from_worker and surplus_worker_caps[target_worker] >= task_weights[from_task]:
    # if surplus_worker_caps[target_worker] > 0:
      print(f"[LB] Move Task-{from_task} to Worker-{w} with the highest surplus cap = {surplus_worker_caps[w]}")
      to_worker = w
    else:
      # If there is no suitable task, swap the min_progress task with the max_progress task
      fastest_task = sorted_tasks[-1]
      if task_to_worker[fastest_task] != from_worker:
        to_task = fastest_task
        to_worker = task_to_worker[to_task]
        print(f"[LB] Swap slowest Task-{from_task} on Worker-{from_worker} with the fastest Task-{to_task} on Worker-{to_worker}")

    try:
      
      if to_worker != -1:
        print(f"[LB] move {from_task} for reassignment")
        # move task to this worker
        action_seq = move_task(from_task, from_worker, to_worker, action_producer, action_seq, task_progress[from_task])
        # Update related variables
        worker_to_task[from_worker].discard(from_task)
        # worker_progress[to_worker] += task_progress[from_task]
        # worker_progress[from_worker] -= task_progress[from_task]
        surplus_worker_caps[to_worker] -= task_weights[from_task]
        worker_to_task[to_worker].add(from_task)
        task_to_worker[from_task] = to_worker
        print(f"[LB] Completed move {from_task} for reassignment")

        if to_task != -1:
          print(f"[LB] move {to_task} back")
          # move a task back if needed
          action_seq = move_task(to_task, to_worker, from_worker, action_producer, action_seq, task_progress[to_task])
          worker_to_task[to_worker].discard(to_task)
          # worker_progress[from_worker] += task_progress[to_task]
          # worker_progress[to_worker] -= task_progress[to_task]
          surplus_worker_caps[from_worker] -= task_weights[to_task]
          worker_to_task[from_worker].add(to_task)
          task_to_worker[to_task] = from_worker
          print(f"[LB] Completed move {to_task} back")
        else:
          print(f"[LB] no swap!")
      else:
        print(f"[LB] no task movement!")
    except Exception as e:
      print(f"[LB] Exception while reassigning tasks: what: {e}")

    print("[LB] Complete task to worker assignment")

    # if not task_assigned:
    #   print("[LB] No task reassignment is made in this round")

    # if task_assigned:
    #   # reset progress tracking after reassignment to make sure their performance is updated
    #   worker_progress = [0 for _ in range(num_workers)]
    #   total_progress = 0
    
    # # subtract smallest progress from all workers to avoid progress accumulation making
    # # progress differences less significant
    # min_worker_progress = min(worker_progress)
    # worker_progress = [worker_progress[i] - min_worker_progress for i in range(num_workers)]
    # total_progress = sum(worker_progress)
  
  print(f"[LB] Load balancing completed after {round} rounds. Exiting task assignment process ...")

#@profile
def main():
  args = parse_arguments()

  # Setup mofka
  print("Setup Mofka ...")
  mofka_dist = MofkaDist(group_file=args.group_file, batchsize=args.batchsize)

  # Handshake with Sirt
  print("Handshake with SIRT ...")
  mofka_dist.handshake(args.ntask_sirt, args.num_sinograms, args.num_columns)

  print("Setting up consumer and producer ...")
  consumer = mofka_dist.consumer(topic_name="daq_dist", consumer_name="dist")
  # producer = mofka_dist.producer(topic_name="dist_sirt", producer_name="dist")

  print("Setting up shared memory sender for producer ...")
  NUM_SLOTS = 64
  SLOT_BYTES = args.num_columns * args.num_sinograms * 4
  mofka_sender = ShmSender(name="Mofka", num_slots=NUM_SLOTS, slot_bytes=SLOT_BYTES, init_args=args, flush_producer=flush_mofka_producer)

  # Register a signal handler for this function
  def signal_handler(sig, frame):
    print("\nCtrl+C pressed. Exiting immediately...")
    if args.sst:
      sst_sender.stop(force_kill_after=2.0)
    mofka_sender.stop(force_kill_after=2.0)
    sys.exit(0)  # Exit the program immediately

  signal.signal(signal.SIGINT, signal_handler)
  signal.signal(signal.SIGTERM, signal_handler)

  # print("Setting up consumer and producer for actions ...")
  # # action_mofka_dist = MofkaDist(group_file=args.group_file, batchsize=args.batchsize)
  # action_mofka_dist = mofka_dist
  # action_consumer = action_mofka_dist.consumer(topic_name="sirt_dist_action", consumer_name="dist")
  # action_producer = action_mofka_dist.producer(topic_name="dist_sirt_action", producer_name="dist")

  # # Create a new thread to handle incoming actions from SIRT if needed,
  # # leaving the main thread to handle data distribution.
  # assignment_thread = threading.Thread(
  #   target=task_to_worker_assignment,
  #   args=(action_producer, action_consumer, args, action_mofka_dist),
  #   daemon=True
  # )
  # assignment_thread.start() 

  # Create a new process to run task to worker assignment
  num_workers = mofka_dist.nworkers
  assignment_process = multiprocessing.Process(
    target=task_to_worker_assignment_wrapper,
    args=(args, num_workers),
    daemon=False
  )
  assignment_process.start()

  print("Task assignment process started")

  mofka_producing_time = []
  mofka_consuming_time = []
  # Setup serializer
  serializer = TraceSerializer.ImageSerializer()

  print("Setting up serializer ...")

  # White/dark fields
  white_imgs=[]
  tot_white_imgs=0
  dark_imgs=[]
  tot_dark_imgs=0

  # Receive images
  total_received=0
  total_size=0
  seq=0
  time0 = time.time()

  if args.sst:
    sst_sender = ShmSender(name="SST", num_slots=NUM_SLOTS, slot_bytes=SLOT_BYTES,
                            init_args=args, flush_producer=flush_sst_producer)

  print("Starting to receive images ...")

  if args.sst:
    sst_sender.start_async_sender_thread()
  mofka_sender.start_async_sender_thread()

  # # Create a new thread to periodically flush the producer
  # flush_thread = threading.Thread(target=flush_mofka_producer, args=(mofka_dist,producer,), daemon=False)
  # flush_thread.start()

  # last_ok_time = time.time()
  run_id = f"{os.getpid()}-{int(time.time())}-{uuid.uuid4().hex[:8]}"

  while True:

    mofka_metadata, mofka_data, pull_times = mofka_dist.pull_image(consumer)
    if mofka_metadata["Type"] == "FIN": break
    sequence_id = mofka_metadata["sequence_id"]
    total_received += 1
    total_size += len(mofka_data)
    mofka_consuming_time.append(pull_times)
    # This is mostly for data rate tests
    if args.skip_serialize:
      print("Skipping rest. Received msg: {}".format(total_received))
      continue

    # Deserialize msg to image
    mofka_read_image = serializer.deserialize(serialized_image=mofka_data)
    serializer.info(mofka_read_image) # print image information

    # # Local checks
    # if args.check_seq:
    #   if seq != mofka_read_image.Seq():
    #     print("Wrong sequence number: {} != {}".format(seq, mofka_read_image.Seq()))
    #   seq += 1

    # Push image to workers (REQ/REP)
    my_image_np = mofka_read_image.TdataAsNumpy()
    if args.uint8_to_float32:
      my_image_np.dtype = np.uint8
      sub = np.array(my_image_np, dtype="float32")
    elif args.uint16_to_float32:
      my_image_np.dtype = np.uint16
      sub = np.array(my_image_np, dtype="float32")
    elif args.cast_to_float32:
      my_image_np.dtype=np.float32
      sub = my_image_np
    else: sub = my_image_np

    sub = sub.reshape((1, mofka_read_image.Dims().Y(), mofka_read_image.Dims().X()))
    # If incoming data is projection
    if mofka_read_image.Itype() is serializer.ITypes.Projection:
      rotation=mofka_read_image.Rotation()
      if args.degree_to_radian: rotation = rotation*math.pi/180.

      # Tomopy operations expect 3D data, reshape incoming projections.
      if args.normalize:
        # flat/dark fields' corresponding rows
        if tot_white_imgs>0 and tot_dark_imgs>0:
          # print("normalizing: white_imgs.shape={}; dark_imgs.shape={}".format(
                  #np.array(white_imgs).shape, np.array(dark_imgs).shape))
          sub = tp.normalize(sub, flat=white_imgs, dark=dark_imgs)
      if args.remove_stripes:
        #print("removing stripes")
        sub = tp.remove_stripe_fw(sub, level=7, wname='sym16', sigma=1, pad=True)
      if args.mlog:
        #print("applying -log")
        sub = -np.log(sub)
      if args.remove_invalids:
        #print("removing invalids")
        sub = tp.remove_nan(sub, val=0.0)
        sub = tp.remove_neg(sub, val=0.00)
        sub[np.where(sub == np.inf)] = 0.00

      #to send from mofka:
      # mofka_sub = sub.flatten()
      ncols = sub.shape[2]

      mofka_sub = np.ascontiguousarray(sub, dtype=np.float32).ravel()

      if args.sst:
        # print(f"Sending image seq_id {sequence_id} to sirt through SST")
        # tt = sst_dist.push_image(mofka_sub, sequence_id, args.num_sinograms, ncols, rotation,
        #                 mofka_read_image.UniqueId(), mofka_read_image.Center())
        sst_sender.async_enqueue_image(
          data=mofka_sub,
          sequence_id=sequence_id,
          num_sinograms=args.num_sinograms,
          num_columns=ncols,
          rotation=rotation,
          unique_id=mofka_read_image.UniqueId(),
          center=mofka_read_image.Center(),
          msg_id=f"{run_id}:{sequence_id}",
          timeout=0.2
        )
        
        # print(f"Sending image seq_id {sequence_id} to sirt through Mofka")
        # tt = mofka_dist.push_image(mofka_sub, sequence_id, args.num_sinograms, ncols, rotation,
        #                 mofka_read_image.UniqueId(), mofka_read_image.Center(), producer=producer)

      # Queueing in a seperate thread
      # print(f"Queueing image seq_id {sequence_id} to sirt through Mofka")
      # # mofka_queue.put(ImagePacket(mofka_sub, sequence_id, args.num_sinograms, ncols, rotation,
      # #                 mofka_read_image.UniqueId(), mofka_read_image.Center()))
      # msg_id = f"{run_id}:{sequence_id}"   # stable id for logging / manual dedup if needed
      # ok = sender.enqueue_image(
      #   data=mofka_sub.tobytes(),
      #   sequence_id=sequence_id,
      #   num_sinograms=args.num_sinograms,
      #   num_columns=ncols,
      #   rotation=rotation,
      #   unique_id=mofka_read_image.UniqueId(),
      #   center=mofka_read_image.Center(),
      #   msg_id=msg_id,
      #   timeout=0.2
      # )

      # # periodically
      # sender.maybe_restart_if_stalled()

      # if not ok:
      #   # backpressure: pause or spill
      #   time.sleep(0.01)
      
      # mofka_sub = np.ascontiguousarray(sub, dtype=np.float32).ravel()
      mofka_sender.async_enqueue_image(
        data=mofka_sub,
        sequence_id=sequence_id,
        num_sinograms=args.num_sinograms,
        num_columns=ncols,
        rotation=rotation,
        unique_id=mofka_read_image.UniqueId(),
        center=mofka_read_image.Center(),
        msg_id=f"{run_id}:{sequence_id}",
        timeout=0.2
      )

      # if all(isinstance(item, list) for item in tt):
      #   mofka_producing_time.extend(tt)
      # else:
      #   mofka_producing_time.append(tt)

    # If incoming data is white field
    if mofka_read_image.Itype() is serializer.ITypes.White:
      #print("White field data is received: {}".format(read_image.UniqueId()))
      white_imgs.extend(sub)
      tot_white_imgs += 1

    # If incoming data is white-reset
    if mofka_read_image.Itype() is serializer.ITypes.WhiteReset:
      #print("White-reset data is received: {}".format(read_image.UniqueId()))
      white_imgs=[]
      white_imgs.extend(sub)
      tot_white_imgs += 1

    # If incoming data is dark field
    if mofka_read_image.Itype() is serializer.ITypes.Dark:
      #print("Dark data is received: {}".format(read_image.UniqueId()))
      dark_imgs.extend(sub)
      tot_dark_imgs += 1

    # If incoming data is dark-reset
    if mofka_read_image.Itype() is serializer.ITypes.DarkReset:
      #print("Dark-reset data is received: {}".format(read_image.UniqueId()))
      dark_imgs=[]
      dark_imgs.extend(sub)
      tot_dark_imgs += 1
    seq+=1

  if args.sst:
    print("Drainning SST stream then sending FIN to sirt")
    deadline = time.time() + 10.0
    while time.time() < deadline:
        sst_sender.poll()
        sst_sender.maybe_restart_if_stalled()
        if (not sst_sender.inflight) and (not sst_sender.pending_definite):
            break
        time.sleep(0.01)
    fin_deadline = time.time() + 5.0
    sent = False
    while time.time() < fin_deadline and not sent:
      sst_sender.poll()
      sent = sst_sender.send_fin(timeout=0.2)
      if not sent:
        time.sleep(0.01)

  print("Stopping shared memory mofka_sender ...")
  # drain until we have no known-safe pending/inflight
  deadline = time.time() + 10.0
  while time.time() < deadline:
    mofka_sender.poll()
    mofka_sender.maybe_restart_if_stalled()  # optional: keep it alive while draining
    if (not mofka_sender.inflight) and (not mofka_sender.pending_definite):
      break
    time.sleep(0.01)

  if mofka_sender.inflight or mofka_sender.pending_definite:
    print("[WARN] Still have inflight/pending_definite at shutdown. "
            "Not sending FIN to avoid loss. Investigate backpressure/hang.")
  else:
    fin_deadline = time.time() + 5.0
    sent = False
    while time.time() < fin_deadline and not sent:
      mofka_sender.poll()
      sent = mofka_sender.send_fin(timeout=0.2)
      if not sent:
        time.sleep(0.01)

    if not sent:
      print("[WARN] Could not enqueue FIN (desc_q full). Forcing stop.")
    else:
      mofka_sender.wait_worker_exit(timeout=5.0)
  if mofka_sender.pending_uncertain:
    print(f"[WARN] {len(mofka_sender.pending_uncertain)} uncertain messages parked; "
          "not resent to minimize duplicates.")
  mofka_sender.stop(force_kill_after=2.0)      # now force if still stuck
  

  # print("Notifying SIRT that we are done ...")
  # for w in range(mofka_dist.nworkers):
  #   action_info = {
  #       "Type": "SHUTDOWN",
  #       "worker_id": w
  #   }
  #   action_producer.push(action_info)
  #   action_producer.flush()
  
  # del action_producer
  # del action_consumer

  print("Cleaning up task assignment process ...")
  # args.dynamic_loadbalancing = "false"

  # Wait for the assignment process to finish
  while args.dynamic_loadbalancing.lower() == "true":
    time.sleep(1)
  assignment_process.join()
  # if assignment_process.is_alive():
  #   assignment_process.terminate()

  # print("Complete data disitribution, sleeping until to exit ...")
  # while True:
  #   time.sleep(1)

  # t = mofka_dist.last_flush(producer)
  # if t is not None:
  #   mofka_producing_time.append(t)
  time1 = time.time()

  # Profile information
  elapsed_time = time1-time0
  tot_MiBs = (total_size*1.)/2**20
  print("Received number of projections: {}; Total size (MiB): {:.2f}; Elapsed time (s): {:.2f}".format(total_received, tot_MiBs, elapsed_time))
  print("Rate (MiB/s): {:.2f}; (msg/s): {:.2f}".format(
            tot_MiBs/elapsed_time, total_received/elapsed_time))

  # mofka_dist.done_image(producer)

  # fields = ["type", "projection_id", "start", "stop", "duration", "metadata_size" ,"data_size"]
  # with open(args.logdir + '/Dist_push.csv', 'w') as f:
  #   write = csv.writer(f)
  #   write.writerow(fields)
  #   write.writerows(mofka_producing_time)
  fields = ["t_wait", "t_metadata", "metadata_size" ,"t_data", "data_size"]
  with open(args.logdir + '/Dist_pull.csv', 'w') as f:
    write = csv.writer(f)
    write.writerow(fields)
    write.writerows(mofka_consuming_time)
  # del producer
  del consumer

  if args.sst:
    while True:
      time.sleep(1)
    
  print("Exiting ...")

def task_to_worker_assignment_wrapper(args, num_workers):
  """Wrapper to ensure output is flushed to the parent process"""
  sys.stdout.flush()
  sys.stderr.flush()
  task_to_worker_assignment(args, num_workers)
  sys.stdout.flush()
  sys.stderr.flush()

if __name__ == '__main__':
  main()
