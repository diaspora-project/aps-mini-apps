import flatbuffers
from fakapp.common.mona.tracedm import Dim3, IType, TImage


class ImageSerializer:
  """FlatBuffers (de)serializer for TImage messages. Byte-compatible with
  tekapp.common.serializer.ImageSerializer."""

  def __init__(self):
    self.builder = flatbuffers.Builder(0)
    self.ITypes = IType.IType()

  def _resetBuilder(self):
    self.builder.head = len(self.builder.Bytes)
    self.builder.current_vtable = None
    self.builder.minalign = 1
    self.builder.objectEnd = None
    self.builder.vtables = {}
    self.builder.nested = False
    self.builder.finished = False

  def serialize(self, image, uniqueId, itype=None, center=None,
                rotation=None, rotation_step=None, seq=0):
    builder = self.builder

    bytesOfImage = image.tobytes()
    TImage.TImageStartTdataVector(builder, len(bytesOfImage))
    builder.head = builder.head - len(bytesOfImage)
    builder.Bytes[builder.head : (builder.head + len(bytesOfImage))] = bytesOfImage
    img_buf_offset = builder.EndVector(len(bytesOfImage))

    TImage.TImageStart(builder)

    if len(image.shape) == 2:
      image.shape = (1, image.shape[0], image.shape[1])
    TImage.TImageAddDims(builder, Dim3.CreateDim3(builder, *image.shape))
    rotation = rotation if rotation is not None else uniqueId * rotation_step
    TImage.TImageAddRotation(builder, rotation)
    center = center if center is not None else image.shape[-1] / 2.
    TImage.TImageAddCenter(builder, center)
    TImage.TImageAddUniqueId(builder, uniqueId)
    TImage.TImageAddItype(builder, itype)
    TImage.TImageAddTdata(builder, img_buf_offset)
    TImage.TImageAddSeq(builder, seq)
    serialized_image_offset = TImage.TImageEnd(builder)

    builder.Finish(serialized_image_offset)
    serialized_data = builder.Output()
    self._resetBuilder()
    return serialized_data

  def deserialize(self, serialized_image, root_offset=0):
    return TImage.TImage.GetRootAsTImage(serialized_image, root_offset)

  def info(self, timage):
    if not isinstance(timage, TImage.TImage):
      print("Not an instance of TImage.TImage: {}".format(timage))
    print("Image seq id={}; unique id={}; dims={}; rotation={}; center={}; itype={}".format(
        timage.Seq(), timage.UniqueId(),
        (timage.Dims().Y(), timage.Dims().X()),
        timage.Rotation(), timage.Center(), timage.Itype()))
