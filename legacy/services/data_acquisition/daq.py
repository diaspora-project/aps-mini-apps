import time
import daq_communicator 
from daq_argparser import parse_arguments
from logger_config import logger


def main():
  args = parse_arguments()

  # Set up the publisher socket
  comm = daq_communicator.DAQCommunication(
      data_plane_addr=args.data_plane_addr,
      data_plane_hwm=args.data_plane_hwm,
      control_plane_addr=args.control_plane_addr
  )

  time0 = time.time()
  
  if args.mode == 0: # Test data acquisition
    from daq_simulate_random import simulate_random

    simulate_random(publisher_socket=comm.publisher_socket,
              num_sinograms=args.num_sinograms,                       # Y
              num_sinogram_columns=args.num_sinogram_columns,         # X 
              num_sinogram_projections=args.num_sinogram_projections, # Z
              slp=args.iteration_sleep)


  elif args.mode == 1: # Simulate data acquisition with a file
    from daq_simulate_from_file import simulate_from_file

    logger.info(f"Simulating data acquisition on file: {args.simulation_file}; iteration: {args.d_iteration}")
    simulate_from_file(publisher_socket=comm.publisher_socket, 
              input_f=args.simulation_file,
              beg_sinogram=args.beg_sinogram, num_sinograms=args.num_sinograms,
              iteration=args.d_iteration,
              slp=args.iteration_sleep, prj_slp=0.6)
  
  
  else:
    logger.error(f"Unknown mode: {args.mode}")
    raise ValueError(f"Unknown mode: {args.mode}")

  logger.info(f"Total time (s): {time.time() - time0:.2f}")


if __name__ == '__main__':
    main()
