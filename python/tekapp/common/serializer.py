import numpy as np
import time
import flatbuffers
from tekapp.common.mona.tracedm import Dim3, IType, TImage

class ImageSerializer:
  """
  Serialize image data using flatbuffers.

  """

  def __init__(self):
    r"""
    Serialization class for detector image data.

    Parameters
    ----------
    builder : flatbuffer.Builder
      Used for buffer allocation and copying image data to serialization
      buffer.

    """
    self.builder = flatbuffers.Builder(0)
    self.ITypes = IType.IType()

  def _resetBuilder(self):
    r"""
    Resets the serialization builder without deallocating the memory.
    """
    self.builder.head = len(self.builder.Bytes)
    self.builder.current_vtable = None
    self.builder.minalign = 1
    self.builder.objectEnd = None
    self.builder.vtables = {}
    self.builder.nested = False
    self.builder.finished = False


  def serialize(self, image, uniqueId, itype=None, center=None, rotation=None, rotation_step=None, seq=0):
    r"""
    Serializes the provided image data using builder.

    Serialization function changes internal offset address of the
    builder.head depending on the size of the image.

    Parameters
    ----------
    image : numpy.array
      3D image data in numpy array format. Currently this data is being
      converted to sequence of bytes and copied to serialization buffer.

    uniqueId : np.int32
      Image's unique id.

    itype : IType
      Image's type. Currently, five options are supported (Projection,
      White, Dark, WhiteReset, DarkReset.) The remaining pipeline runs
      according to this type information.

    center : np.float32
      Center information of the image. If nothing is passed, then center
      will be derived with:
      ... math:: center=image.shape[1]/2.

    rotation : np.float32
      Rotation information of the image. If nothing is passed, then
      rotation will be derived with:
      ... math:: rotation=uniqueId*rotation_step

    rotation_step : np.float32
      Rotation change for each collected image. If set to None, then
      `rotation` parameter needs to be set.

    seq : np.uint32
      Sequence number of the packet.

    Returns
    -------
    serialized_data : bytearray
      Serialized image data.
    """

    builder = self.builder

    # Prepare image buffer in serialization buffer
    bytesOfImage = image.tobytes()
    TImage.TImageStartTdataVector(builder, len(bytesOfImage))
    builder.head=builder.head - len(bytesOfImage)
    builder.Bytes[builder.head : (builder.head + len(bytesOfImage))] = bytesOfImage
    img_buf_offset = builder.EndVector(len(bytesOfImage))

    # Start serialization
    TImage.TImageStart(builder)

    if len(image.shape) == 2: image.shape = (1, image.shape[0], image.shape[1])
    TImage.TImageAddDims(builder, Dim3.CreateDim3(builder, *image.shape))
    rotation = rotation if rotation is not None else uniqueId*rotation_step
    TImage.TImageAddRotation(builder, rotation)
    center = center if center is not None else image.shape[-1]/2.
    TImage.TImageAddCenter(builder, center)
    TImage.TImageAddUniqueId(builder, uniqueId)
    TImage.TImageAddItype(builder, itype)
    TImage.TImageAddTdata(builder, img_buf_offset)
    TImage.TImageAddSeq(builder, seq)
    serialized_image_offset = TImage.TImageEnd(builder)

    builder.Finish(serialized_image_offset)
    # End serialization

    serialized_data = builder.Output()

    self._resetBuilder()

    return serialized_data

  def deserialize(self, serialized_image, root_offset=0):
    r"""
    Deserializes the provided byte sequence image data using TImage.
    """
    image = TImage.TImage.GetRootAsTImage(serialized_image, root_offset)
    return image

  def info(self, timage):
    if not isinstance(timage, TImage.TImage):
      print("Not an instance of TImage.TImage: {}".format(timage))
    print("Image seq id={}; unique id={}; dims={}; rotation={}; center={}; itype={}"
            .format(timage.Seq(), timage.UniqueId(), (timage.Dims().Y(), timage.Dims().X()),
                    timage.Rotation(), timage.Center(), timage.Itype()))
