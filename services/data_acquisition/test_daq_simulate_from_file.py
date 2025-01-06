import os
import sys
import pytest
import numpy as np
from unittest.mock import patch, MagicMock
import signal
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..')))
from services.data_acquisition.daq_simulate_from_file import DAQSimulator

@pytest.fixture
def simulator():
    return DAQSimulator()

def test_signal_handler(simulator):
    with patch('services.data_acquisition.daq_simulate_from_file.logger') as mock_logger:
        simulator._signal_handler(signal.SIGINT, None)
        assert simulator.bsignal is True
        mock_logger.info.assert_called_with("Signal handler called with signal 2")

def test_read_hdf5_file_not_exists(simulator):
    with patch('services.data_acquisition.daq_simulate_from_file.logger') as mock_logger, \
         patch('os.path.exists', return_value=False), \
         patch('sys.exit') as mock_exit:
        with pytest.raises(FileNotFoundError):
            simulator._read_hdf5_file('non_existent_file.h5')

def test_read_hdf5_file_exists(simulator):
    with patch('services.data_acquisition.daq_simulate_from_file.logger') as mock_logger, \
         patch('os.path.exists', return_value=True), \
         patch('h5py.File', return_value=MagicMock()) as mock_h5:
        mock_h5.return_value.__enter__.return_value = {
            'exchange/data': np.array([1, 2, 3]),
            'exchange/data_white': np.array([4, 5, 6]),
            'exchange/data_dark': np.array([7, 8, 9]),
            'exchange/theta': np.array([10, 11, 12])
        }
        idata, flat, dark, itheta = simulator._read_hdf5_file('existent_file.h5')
        assert np.array_equal(idata, [1, 2, 3])
        assert np.array_equal(flat, [4, 5, 6])
        assert np.array_equal(dark, [7, 8, 9])
        assert np.array_equal(itheta, [10, 11, 12])
        mock_logger.info.assert_called()

def test_convert_radian(simulator):
    itheta = np.array([0, 90, 180])
    converted = simulator._convert_radian(itheta)
    assert np.allclose(converted, [0, np.pi/2, np.pi])

def test_setup_simulation_data(simulator):
    with patch.object(simulator, '_read_hdf5_file', return_value=(
        np.array([1, 2, 3]), np.array([4, 5, 6]), np.array([7, 8, 9]), np.array([10, 11, 12])
    )), patch.object(simulator, '_convert_radian', return_value=np.array([0, np.pi/2, np.pi])):
        idata, flat, dark, itheta = simulator._setup_simulation_data('file.h5')
        assert np.array_equal(idata, [1, 2, 3])
        assert np.array_equal(flat, [4, 5, 6])
        assert np.array_equal(dark, [7, 8, 9])
        assert np.array_equal(itheta, [0, np.pi/2, np.pi])

def test_ordered_subset(simulator):
    result = simulator._ordered_subset(10, 3)
    assert np.array_equal(result, [0, 3, 6, 9, 1, 4, 7, 2, 5, 8])

def test_serialize_dataset(simulator):
    with patch('services.data_acquisition.daq_simulate_from_file.TraceSerializer.ImageSerializer') as MockSerializer:
        mock_serializer = MockSerializer.return_value
        mock_serializer.ITypes.WhiteReset = 0
        mock_serializer.ITypes.White = 1
        mock_serializer.ITypes.DarkReset = 2
        mock_serializer.ITypes.Dark = 3
        mock_serializer.ITypes.Projection = 4
        mock_serializer.serialize.return_value = b'serialized_data'

        idata = np.array([1, 2, 3])
        flat = np.array([4, 5, 6])
        dark = np.array([7, 8, 9])
        itheta = np.array([0, np.pi/2, np.pi])

        data = simulator._serialize_dataset(idata, flat, dark, itheta)
        assert len(data) == 9
        assert all(d == b'serialized_data' for d in data)

def test_simulate_from_file(simulator):
    with patch.object(simulator, '_setup_simulation_data', return_value=(
        np.array([1, 2, 3]), np.array([4, 5, 6]), np.array([7, 8, 9]), np.array([0, np.pi/2, np.pi])
    )), patch.object(simulator, '_serialize_dataset', return_value=np.array([b'data1', b'data2', b'data3'])), \
         patch('services.data_acquisition.daq_simulate_from_file.np.load', return_value=[b'data1', b'data2', b'data3']), \
         patch('services.data_acquisition.daq_simulate_from_file.logger') as mock_logger, \
         patch('time.sleep'), patch('services.data_acquisition.daq_simulate_from_file.DAQSimulator._ordered_subset', return_value=[0, 1, 2]):
        mock_socket = MagicMock()
        seq = simulator.simulate_from_file(mock_socket, 'file.h5', iteration=1)
        assert seq == 0
        mock_socket.send.assert_called()
        mock_logger.info.assert_called()

def test_simulate_from_file_1(simulator):
    with patch.object(simulator, 'publisher_socket', MagicMock()), \

    data_file = "/Users/tbicer/Projects/aps-mini-apps/data/tomo_00058_all_subsampled1p_s1079s1081.h5"
    idata, flat, dark, itheta = simulator._read_hdf5_file(data_file)


    simulator._serialize_dataset()