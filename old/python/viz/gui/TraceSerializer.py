import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), './local'))
sys.path.append(os.path.join(os.path.dirname(__file__), './serialization'))
import numpy as np
import time
import flatbuffers
import MONA.TraceDS.Dim2 as Dim2
import MONA.TraceDS.TImage as TImage

class ImageSerializer:
  """
  Serialize image data using flatbuffers.

  """
  
  def __init__(self, builder):
    r"""
    Serialization class for detector image data. 

    Parameters
    ----------
    builder : flatbuffer.Builder
      Used for buffer allocation and copying image data to serialization
      buffer. 

    """
    self.builder = builder


  def serialize(self, image, uniqueId, center=None, rotation=None, rotation_step=None, seq=0):
    r"""
    Serializes the provided image data using builder.

    Serialization function changes internal offset address of the
    builder.head depending on the size of the image.

    Parameters
    ----------
    image : numpy.array
      2D image data in numpy array format. Currently this data is being
      converted to sequence of bytes and copied to serialization buffer.

    uniqueId : np.int32
      Image's unique id. This information will be used for detecting

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

    Notes
    -----
    This code treats image data as byte sequence, which can provide better
    performance but it is not portable. We know that lyra machine is little endian,
    thus there is no issues. However, if needed Flatbuffer documentation mentions 
    builder.Prepend*(val) function can be used to copy data in portable way.

    Example for portable code is given below:
    >>> for i in reversed(image): # Assuming image is uint16.
    >>>  builder.PrependUint16(i)


    Some serialization performance information:
    We note the following serialization times (on lyra machine) when dimensions are
    2048*2048.  
    Serialization time: 0.009891748428344727 - 0.014806032180786133 (mostly in 0.011* range)

    Below is the rough ratios for some of the time consuming steps:
    >>> bytesOfImage = image.tobytes() # Converting byte is expensive (3x/4)
    >>> TImage.TImageStartTdataVector(builder, len(bytesOfImage)) # Memory allocation is expensive (2x-3x)
    >>> builder.Bytes[builder.head : (builder.head + len(bytesOfImage))] = bytesOfImage # Copy operation is expeinsive (x)

    >>> # Image serialization time after preparation: 9.560585021972656e-05

    >>> serialized_data = builder.Output() # Timing range: 0.0007877349853515625 - 0.0009405612945556641
    """

    builder = self.builder

    # Prepare image buffer in serialization buffer
    bytesOfImage = image.tobytes()
    TImage.TImageStartTdataVector(builder, len(bytesOfImage)) 
    builder.head=builder.head - len(bytesOfImage)
    builder.Bytes[builder.head : (builder.head + len(bytesOfImage))] = bytesOfImage 
    img_buf_offset = builder.EndVector(len(bytesOfImage))

    # Image serialization
    TImage.TImageStart(builder)
    TImage.TImageAddDims(builder, Dim2.CreateDim2(builder, image.shape[0], image.shape[1]))
    rotation = rotation if rotation is not None else uniqueId*rotation_step
    TImage.TImageAddRotation(builder, rotation)
    center = center if center is not None else image.shape[1]/2.
    TImage.TImageAddCenter(builder, center)
    TImage.TImageAddUniqueId(builder, uniqueId)
    TImage.TImageAddTdata(builder, img_buf_offset)
    TImage.TImageAddSeq(builder, seq)
    serialized_image_offset = TImage.TImageEnd(builder)
    builder.Finish(serialized_image_offset)

    serialized_data = builder.Output()

    return serialized_data

  def deserialize(self, serialized_image, root_offset=0):
    r"""
    Deserializes the provided byte sequence image data using TImage.

    Parameters
    ----------
    serialized_image : bytearray
      Serialized image using 'TraceImageSerializer.serialize()' function.

    root_offset : np.int32
      It deserialization is being done using builder.Bytes, then the
      offset of `builder.Head()` needs to be passed since deserialization
      is being done backward.


    Returns
    -------
    image : flatbuf.MONA.TraceDS.TImage.TImage
      Deserialized image data.


    Notes
    -----
    Some deserialization performance information:
    We note the following deserialization times (on lyra machine) when dimensions are
    2048*2048: 1.1920928955078125e-05
    """

    image = TImage.TImage.GetRootAsTImage(serialized_image, root_offset)
    return image

  def info(self, timage):
    if not isinstance(timage, TImage.TImage):
      print("Not an instance of TImage.TImage: {}".format(timage))
    print("Image unique id={}; dims={}; rotation={}; center={}"
            .format(timage.UniqueId(), (timage.Dims().Y(), timage.Dims().X()), 
                    timage.Rotation(), timage.Center()))
