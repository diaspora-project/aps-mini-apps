import time
import pvaccess

class TImageTransfer:
  def __init__(self, publisher_socket, pv_image,
                beg_sinogram=0, num_sinograms=0, seq=0):

    self.publisher_socket = publisher_socket
    self.pv_image = pv_image
    self.beg_sinogram = beg_sinogram
    self.num_sinograms = num_sinograms
    self.seq = seq
    self.pv_channel = None
    self.lastImageId=0
    self.mlists=[]


  def __enter__(self):
    self.pv_channel = pvaccess.Channel(self.pv_image)
    x, y = self.pv_channel.get('field()')['dimension']
    self.dims=(y['size'], x['size'])
    labels = [item["name"] for item in self.pv_channel.get('field()')["attribute"]]
    self.theta_key = labels.index("SampleRotary")
    self.scan_delta_key = labels.index("ScanDelta")
    self.start_position_key = labels.index("StartPos")
    self.last_save_dest = labels.index("SaveDest")
    self.imageId = labels.index("ArrayCounter")
    self.flat_count=0
    self.dark_count=0
    self.flat_counter=0
    self.dark_counter=0
    self.sequence=0
    print(self.dims)
    if self.num_sinograms>0:
      if (self.beg_sinogram<0) or (self.beg_sinogram+self.num_sinograms>self.dims[0]): 
        raise Exception("Exceeds the sinogram boundary: {} vs. {}".format(
                            self.beg_sinogram+self.num_sinograms, self.dims[0]))
      self.beg_index = self.beg_sinogram*self.dims[1]
      self.end_index = self.beg_sinogram*self.dims[1] + self.num_sinograms*self.dims[1]
    self.pv_channel.subscribe('push_image_data', self.push_image_data)

    return self


  def start_monitor(self, smon="value,attribute,uniqueId"):
    self.pv_channel.startMonitor(smon)
    while True: time.sleep(60)  # Forever monitor


  def push_image_data(self, data):
    img = np.frombuffer(data['value'][0]['ubyteValue'], dtype=np.uint8)
    #uniqueId = data['uniqueId']
    uniqueId = data["attribute"][self.imageId]["value"][0]["value"]
    scan_delta = data["attribute"][self.scan_delta_key]["value"][0]["value"]
    start_position = data["attribute"][self.start_position_key]["value"][0]["value"]

    serializer = TraceSerializer.ImageSerializer()

    itype=data["attribute"][self.last_save_dest]["value"][0]["value"]
    if itype == "/exchange/data":
      print("Projection data: {}".format(uniqueId))
      itype=serializer.ITypes.Projection
      if self.sequence > 1:
        self.sequence=0
        self.flat_count=self.flat_counter
        self.dark_count=self.dark_counter
        self.flat_counter=0
        self.dark_counter=0
    if itype == "/exchange/data_white":
      print("White field: {}".format(uniqueId))
      itype=serializer.ITypes.White
      self.sequence+=1; 
      self.flat_counter+=1
    if itype == "/exchange/data_dark":
      print("Dark field: {}".format(uniqueId))
      itype=serializer.ITypes.Dark
      self.sequence+=1; 
      self.dark_counter+=1

    # XXX with flat/dark field data uniquId is not a correct value any more
    theta = ((start_position + (uniqueId-(self.flat_count+self.dark_count)))*scan_delta) % 360.0
    diff = self.lastImageId-(uniqueId-1)
    self.lastImageId = uniqueId
    print("UniqueID={}, Rotation Angle={}; Id Check={}; iType={}; scandelta={}; #flat={}; #dark={}, startposition={}".format(
                                    uniqueId, theta, diff, itype, scan_delta, self.flat_count, self.dark_count, start_position))
    self.mlists.append([uniqueId, self.flat_count, self.dark_count, scan_delta, start_position, theta])

    if self.num_sinograms!=0:
      img = img[self.beg_index : self.end_index]
      img = img.reshape((self.num_sinograms, self.dims[1]))
    else: img = img.reshape(self.dims)
    print("img.shape={}; img={}".format(img.shape, img))

    serialized_data = serializer.serialize(image=img, uniqueId=uniqueId,
                                itype=itype,
                                rotation=theta, seq=self.seq)
    self.publisher_socket.send(serialized_data)
    self.seq+=1


  def __exit__(self, exc_type, exc_value, traceback):
    print("\nTrying to gracefully terminate...")
    self.pv_channel.stopMonitor()
    self.pv_channel.unsubscribe('push_image_data')

    print("Send terminate signal...")
    self.publisher_socket.send("end_data".encode())
    print("Done sending...")
    for i in range(len(self.mlists)):
      print("{}: {}".format(i, self.mlists[i]))
    if exc_type is not None:
      print("{} {} {}".format(exc_type, exc_value, traceback))
      return False 
    return self