import time
import daq_communicator 
from daq_argparser import parse_arguments
from logger_config import logger


def main():
  args = parse_arguments()

  # Set up the publisher socket
  comm = daq_communicator.DAQCommunication( publisher_addr=args.publisher_addr, 
                                            synch_addr=args.synch_addr, 
                                            synch_count=args.synch_count,
                                            publisher_hwm=args.publisher_hwm)

  time0 = time.time()
  if args.mode == 0: # Read data from PV
    from daq_pvaccess import TImageTransfer
    logger.info("Reading data from PV: {}".format(args.image_pv))
    with TImageTransfer(publisher_socket=comm.publisher_socket,
                        pv_image=args.image_pv,
                        beg_sinogram=args.beg_sinogram, 
                        num_sinograms=args.num_sinograms, seq=0) as tdet:
      tdet.start_monitor()  # Infinite loop

  elif args.mode == 1: # Simulate data acquisition with a file
    from daq_simulate_from_file import simulate_from_file
    logger.info(f"Simulating data acquisition on file: {args.simulation_file}; iteration: {args.d_iteration}")
    simulate_from_file(publisher_socket=comm.publisher_socket, 
              input_f=args.simulation_file,
              beg_sinogram=args.beg_sinogram, num_sinograms=args.num_sinograms,
              iteration=args.d_iteration,
              slp=args.iteration_sleep, prj_slp=0.6)
  
  elif args.mode == 2: # Test data acquisition
    from daq_simulate_random import simulate_random
    logger.info(f"Simulating random data acquisition; number of sinograms: {args.num_sinogram}; iteration: {args.d_iteration}") 
    simulate_random(publisher_socket=comm.publisher_socket,
              num_sinograms=args.num_sinograms,                       # Y
              num_sinogram_columns=args.num_sinogram_columns,         # X 
              num_sinogram_projections=args.num_sinogram_projections, # Z
              slp=args.iteration_sleep)
  
  else:
    logger.error(f"Unknown mode: {args.mode}")

  time1 = time.time()
  logger.info("Total time (s): {:.2f}".format(time1 - time0))


if __name__ == '__main__':
    main()
