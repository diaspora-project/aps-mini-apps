import zmq
from argparse import ArgumentParser


# Arguments
parser = ArgumentParser()
parser.add_argument("-rba", "--router_addr", dest="router_addr", 
  required=True,
  help="Router (frontend for REQ)  bind address. E.g. \'tcp://*:50001\'")
parser.add_argument("-dba", "--dealer_addr", dest="dealer_addr",
  required=True,
  help="Dealer (backend for REP) connect address. E.g. for mona \'tcp://164.54.113.143:50001\'")

args = parser.parse_args()


def main():
  context = zmq.Context()

  # Socket facing clients
  frontend = context.socket(zmq.ROUTER)
  frontend.bind(args.router_addr)

  # Socket facing services
  backend  = context.socket(zmq.DEALER)
  backend.connect(args.dealer_addr)

  zmq.proxy(frontend, backend)

  # We never get here…
  frontend.close()
  backend.close()
  context.term()

if __name__ == "__main__":
  main()
