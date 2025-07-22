
import time
from logger_config import logger

def _serialize_dataset(self, idata, flat, dark, itheta, seq=0):
  data = []
  start_index = 0
  time_ser = 0.
  serializer = TraceSerializer.ImageSerializer()

  logger.info("Starting serialization")
  if flat is not None:
    for uniqueFlatId, flatId in zip(range(start_index, start_index + flat.shape[0]), range(flat.shape[0])):
      t_ser0 = time.time()
      dflat = flat[flatId]
      itype = serializer.ITypes.WhiteReset if flatId == 0 else serializer.ITypes.White
      serialized_data = serializer.serialize(image=dflat, uniqueId=uniqueFlatId,
                           itype=itype,
                           rotation=0, seq=seq)
      data.append(serialized_data)
      time_ser += time.time() - t_ser0
      seq += 1
    start_index += flat.shape[0]

  if dark is not None:
    for uniqueDarkId, darkId in zip(range(start_index, start_index + dark.shape[0]), range(dark.shape[0])):
      t_ser0 = time.time()
      dflat = dark[flatId]
      itype = serializer.ITypes.DarkReset if darkId == 0 else serializer.ITypes.Dark
      serialized_data = serializer.serialize(image=dflat, uniqueId=uniqueDarkId,
                           itype=itype,
                           rotation=0, seq=seq)
      time_ser += time.time() - t_ser0
      seq += 1
      data.append(serialized_data)
    start_index += dark.shape[0]

  for uniqueId, projId, rotation in zip(range(start_index, start_index + idata.shape[0]),
                      range(idata.shape[0]), itheta):
    t_ser0 = time.time()
    proj = idata[projId]
    itype = serializer.ITypes.Projection
    serialized_data = serializer.serialize(image=proj, uniqueId=uniqueId,
                         itype=itype,
                         rotation=rotation, seq=seq)
    time_ser += time.time() - t_ser0
    seq += 1
    data.append(serialized_data)

  logger.info("Serialization time={:.2f}".format(time_ser))
  return np.array(data, dtype=object)