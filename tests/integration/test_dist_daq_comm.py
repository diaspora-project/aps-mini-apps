import multiprocessing
import time
import sys
import os
import pytest

# Adjust import paths for testing
DIST_SRC = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../services/dist/src'))
DAQ_SRC = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../services/daq/src'))
sys.path.insert(0, DIST_SRC)
sys.path.insert(0, DAQ_SRC)

from dist_comm import DIST_Comm  # from services/dist/src/dist_comm.py
from daq import main as daq_main  # from services/daq/src/daq.py

def run_daq():
    daq_main()

@pytest.fixture(scope="module")
def daq_process():
    proc = multiprocessing.Process(target=run_daq)
    proc.start()
    time.sleep(1)  # Give the router time to start
    yield
    proc.terminate()
    proc.join()

def test_dist_daq_communication(daq_process):
    try:
        dist_comm = DIST_Comm(identity="dist_test")
        test_message = b"Integration test message"
        dist_comm.control_send(test_message)
        reply = dist_comm.control_receive()
        assert reply == b"ACK: " + test_message
        print(f"{test_message} -> {reply}")
    finally:
        print("Terminating test and cleaning up, dealer, ZMQ context.")
        dist_comm.close()
