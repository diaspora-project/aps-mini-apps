import zmq


def synchronize_subs(context, subscriber_count, bind_address_rep):
  # Prepare synch. sockets
  sync_socket = context.socket(zmq.REP)
  sync_socket.bind(bind_address_rep)

  counter = 0
  print("Waiting {} subscriber(s) to synchronize...".format(subscriber_count))
  while counter < subscriber_count:
    msg = sync_socket.recv() # wait for subscriber
    sync_socket.send(b'') # reply ack
    counter += 1
    print("Subscriber joined: {}/{}".format(counter, subscriber_count))

def main():

  context = zmq.Context()
  synchronize_subs(context, 3, 'tcp://*:50001')

  print('Done with the communication')

if __name__ == "__main__":
  main()
