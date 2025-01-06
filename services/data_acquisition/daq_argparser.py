import argparse

def parse_arguments():
  parser = argparse.ArgumentParser(
          description='Data Acquisition Process Simulator')

  parser.add_argument('--mode', type=int, required=True,
                      help='Data acqusition mod (0=detector; 1=simulate; 2=test)')

  parser.add_argument("--image_pv", help="EPICS image PV name.")

  parser.add_argument('--publisher_addr', default="tcp://*:50000",
                      help='Publisher addresss of data source process.')
  parser.add_argument('--publisher_hwm', type=int, default=0,
                      help='Sets high water mark value for publisher.')

  parser.add_argument('--synch_addr', help='Waits for all subscribers to join.')
  parser.add_argument('--synch_count', type=int, default=1,
                      help='Number of expected subscribers.')

  parser.add_argument('--simulation_file', help='File name for mock data acquisition. ')
  parser.add_argument('--d_iteration', type=int, default=1,
                      help='Number of iteration on simulated data.')
  parser.add_argument('--iteration_sleep', type=float, default=0,
                      help='Delay data publishing for each iteration.')
  parser.add_argument('--beg_sinogram', type=int, default=0,
                      help='Starting sinogram for reconstruction.')
  parser.add_argument('--num_sinograms', type=int, default=0,
                      help='Number of sinograms to reconstruct.')
  parser.add_argument('--num_sinogram_columns', type=int,
                      help='Number of columns per sinogram.')
  parser.add_argument('--num_sinogram_projections', type=int,
                      help='Number of projections per sinogram.')
  
  args = parser.parse_args()
  
  # Custom logic to handle dependent parameters
  if args.mode == 1 and not args.simulation_file:
    parser.error("--simulation_file is required when --mode is 1 (simulate)")

  if args.mode == 0 and not args.image_pv:
    parser.error("--image_pv is required when --mode is 0 (detector)")

  return args