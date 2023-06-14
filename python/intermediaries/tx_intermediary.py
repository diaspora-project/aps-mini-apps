import zmq
import argparse
import concurrent.futures as futures
import time
import sys


def txparser():
  # Arguments
  parser = argparse.ArgumentParser()

  # Shared queue
  parser.add_argument('--enable_shared_queue', 
    help="Enables shared queue. Shared queue consists of router and dealer sockets. Default value is false.",
    dest='feature_shared_queue', action='store_true')
  parser.set_defaults(feature_shared_queue=False)

  parser.add_argument("--shared_queue_router_addr", dest="router_addr", 
    help="Shared queue router bind address. This is used for synchronization. Default communication behavior is \'bind\'. E.g. \'tcp://*:50001\'")
  parser.add_argument('--connect_from_shared_queue_router', dest='connect_router', action='store_true')
  parser.add_argument('--bind_from_shared_queue_router', dest='connect_router', action='store_false')
  parser.set_defaults(connect_router=False)

  parser.add_argument("--shared_queue_dealer_addr", dest="dealer_addr", 
    help="Shared queue dealer connect/bind address. This is used for synchronization. Default communication behavior is \'connect\'. E.g. \'tcp://164.54.113.143:50001\'")
  parser.add_argument('--connect_from_shared_queue_dealer', dest='connect_dealer', action='store_true')
  parser.add_argument('--bind_from_shared_queue_dealer', dest='connect_dealer', action='store_false')
  parser.set_defaults(connect_dealer=True)

  # Forwarder
  parser.add_argument('--enable_forwarder', 
    help="Enables forwarder. Forwarder consists of xsub and xpub socket pairs. Default value is false.",
    dest='feature_forwarder', action='store_true')
  parser.set_defaults(feature_forwarder=False)

  parser.add_argument("--forwarder_xpub_addr", dest="xpub_addr", 
    help="Forwarder xpub address. This is used for data forwarding. Default communication behavior is \'bind\'. E.g. \'tcp://*:50000\'")
  parser.add_argument('--connect_from_forwarder_xpub', dest='connect_xpub', action='store_true')
  parser.add_argument('--bind_from_forwarder_xpub', dest='connect_xpub', action='store_false')
  parser.set_defaults(connect_xpub=False)

  parser.add_argument("--forwarder_xsub_addr", dest="xsub_addr", 
    help="Forwarder xsub address. This is used for forwarding data to workers. Default communication behavior is \'bind\'. For theta login to outside, connection may need to be xsub to pub so the usage example usage: --connect_from_forwarder_xsub --forwarder_xsub_addr=\'tcp://164.54.113.143:50000\'")
  parser.add_argument('--connect_from_forwarder_xsub', dest='connect_xsub', action='store_true')
  parser.add_argument('--bind_from_forwarder_xsub', dest='connect_xsub', action='store_false')
  parser.set_defaults(connect_xsub=False)

  # ZMQ Options
  parser.add_argument("--zmq_io_threads", dest="zmq_io_threads", 
    required=False, type=int, default=4,
    help="Number of io threads in zmq context. Default value is 2.")

  args=parser.parse_args()

  # Sanity check
  if not (args.feature_shared_queue or args.feature_forwarder):
    print("At least one of the connection types (--enable_shared_queue or --enable_forwarder) needs to be enabled.")
    sys.exit(0)
  if (args.feature_shared_queue and not (args.router_addr is not None and args.dealer_addr is not None)):
    print("Shared queue enabled but the router (--shared_queue_router_addr) and/or dealer (--shared_queue_dealer_addr) addresses haven't been specified.")
    sys.exit(0)
  if (args.feature_forwarder and not (args.xpub_addr is not None and args.xsub_addr is not None )):
    print("Forwarder enabled but the xsub (--forwarder_xsub_addr) and/or xpub (--forwarder_xpub_addr) addresses haven't been specified.")
    sys.exit(0)


  return args


def tracex_shared_queue(params):
  context, router_addr, connect_router, dealer_addr, connect_dealer  = params

  # Socket facing clients
  frontend = context.socket(zmq.ROUTER)
  if connect_router: frontend.connect(router_addr)
  else: frontend.bind(router_addr)

  # Socket facing services
  backend  = context.socket(zmq.DEALER)
  if connect_dealer: backend.connect(dealer_addr)
  else: backend.bind(dealer_addr)

  zmq.proxy(frontend, backend)

  # We never get here…
  frontend.close()
  backend.close()


def tracex_forwarder(params):
  context, xpub_addr, connect_xpub, xsub_addr, connect_xsub = params

  # Socket facing producers
  frontend = context.socket(zmq.XPUB)
  if connect_xpub: frontend.connect(xpub_addr)
  else: frontend.bind(xpub_addr)

  # Socket facing consumers
  backend = context.socket(zmq.XSUB)
  if connect_xsub: backend.connect(xsub_addr)
  else: backend.bind(xsub_addr) 

  zmq.proxy(frontend, backend)

  # We never get here…
  frontend.close()
  backend.close()



def main():
  args = txparser()

  context = zmq.Context(io_threads=args.zmq_io_threads)

  pool = futures.ThreadPoolExecutor()
  futures_list = []
  
  if args.feature_forwarder:
    forwarder_params = (context, args.xpub_addr, args.connect_xpub, args.xsub_addr, args.connect_xsub) 
    txforwarder_future = pool.submit(tracex_forwarder, forwarder_params)
    futures_list.append(txforwarder_future)
    print("Forwarder is initiated.")

  if args.feature_shared_queue:
    shared_queue_params = (context, args.router_addr, args.connect_router, args.dealer_addr, args.connect_dealer) 
    txshared_queue_future = pool.submit(tracex_shared_queue, shared_queue_params)
    futures_list.append(txshared_queue_future)
    print("Shared queue is initiated.")

  print("Waiting for connections.")
  futures.wait(futures_list)

  # Above is infinite loop so should not be in here!
  context.term()



if __name__ == "__main__":
  main()
