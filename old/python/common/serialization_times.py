import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), './local'))
import numpy as np
import time
import flatbuffers
import serialization.MONA.TraceDS.Dim2 as Dim2
import serialization.MONA.TraceDS.TImage as TImage


builder = flatbuffers.Builder(0)

dim0 = 1920
dim1 = 1200

image = np.random.randint(2, size=dim0*dim1, dtype=np.uint16)

# Prepare builder for TImage.tdata
# Below is not really portable, but we know that lyra is little endian
t0 = time.time()
bytesOfImage = image.tobytes() # Converting byte is expensive (3x/4)
TImage.TImageStartTdataVector(builder, len(bytesOfImage)) # Memory allocation is expensive (2x-3x)
builder.head=builder.head - len(bytesOfImage)
builder.Bytes[builder.head : (builder.head + len(bytesOfImage))] = bytesOfImage # Copy operation is expeinsive (x)
data = builder.EndVector(len(bytesOfImage))
t1 = time.time()
print("Image serialization time={}".format(t1-t0))
# Dims=2048*2048
# Timing range 0.009891748428344727 - 0.014806032180786133 (mostly in 0.011* range)


t0 = time.time()
TImage.TImageStart(builder)
TImage.TImageAddDims(builder, Dim2.CreateDim2(builder, dim0, dim1))
TImage.TImageAddTheta(builder, 0.05)
TImage.TImageAddCenter(builder, 1024.6)
TImage.TImageAddUniqueId(builder, 3345)
TImage.TImageAddTdata(builder, data)
serialized_image_offset = TImage.TImageEnd(builder)
builder.Finish(serialized_image_offset)
t1 = time.time()
print("TImage all Add time={}".format(t1-t0)) 
# Dims=2048*2048
# Timing sample point: 9.560585021972656e-05

t0 = time.time()
serialized_image_buffer = builder.Output()
t1 = time.time()
print("Byte output() time={}".format(t1-t0)) 
# Dims=2048*2048
# Timing range: 0.0007877349853515625 - 0.0009405612945556641


# Deserialize
t0 = time.time()
read_image = TImage.TImage.GetRootAsTImage(serialized_image_buffer,0)
t1 = time.time()
print("Deserialization GetRootAsTImage time={}".format(t1-t0)) 
# Dims=2048*2048
# Timing sample point: 1.1920928955078125e-05
t0 = time.time()
my_image = read_image.TdataAsNumpy()
t1 = time.time()
print("Deserialization TdataAsNumpy time={}".format(t1-t0)) 
# Dims=2048*2048
# Timing sample point: 4.7206878662109375e-05

# my_image is 8bit unsigned integer, change its type to 16
# Note: below is not portable
my_image.dtype = np.uint16

print(np.array_equal(my_image, image))
