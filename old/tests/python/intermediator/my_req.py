import zmq


def synchronize_subs(context, publisher_rep_address):
  sync_socket = context.socket(zmq.REQ)
  sync_socket.connect(publisher_rep_address)

  sync_socket.send(b'') # Send synchronization signal
  sync_socket.recv() # Receive reply



def main():
  context = zmq.Context()

  print('Joining the subscriber list..')

  synchronize_subs(context, 'tcp://127.0.0.1:50001')

  print('Done with the join')



if __name__ == '__main__':
  main()
