import time
import numpy as np

def simulate_random(publisher_socket,
              rotation_step=0.25, num_sinograms=0, 
              num_sinogram_columns=2048, seq=0,
              num_sinogram_projections=1440, slp=0):
  print("Sending projections")
  if num_sinograms<1: num_sinograms=2048
  # Randomly generate image data
  dims=(num_sinograms, num_sinogram_columns)
  image = np.array(np.random.randint(2, size=dims), dtype='uint16')

  serializer = TraceSerializer.ImageSerializer()

  for uniqueId in range(num_sinogram_projections):
    serialized_data = serializer.serialize(image=image, uniqueId=uniqueId+7,
                                      itype=serializer.ITypes.Projection, 
                                      rotation_step=rotation_step, seq=seq) 
    seq+=1
    publisher_socket.send(serialized_data)
    time.sleep(slp)

  return seq
