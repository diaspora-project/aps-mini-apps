import sys
import io
import subprocess

GB=1024*1024*1024
nmsg=(5898240, 2949120, 1474560 ,2949120 ,5898240, 10*GB/(8192*2), 10*GB/(8192*4), 10*GB/(8192*8), 10*GB/(8192*16), 10*GB/(256*1024), 10*GB/(512*1024), 10*GB/(1024*1024))
smsg=(512+1 ,1024+1 ,2048+1 ,4096+1 ,8192+1, (8192*2+1), (8192*4+1), (8192*8+1), (8192*16+1), 256*1024+1, 512*1024+1, 1024*1024+1)

#nmsg=(10*GB/(8192*2+1), 10*GB/(8192*4+1), 10*GB/(8192*8+1))
#smsg=((8192*2+1), (8192*4+1), (8192*8+1))

rep=6
port=5555

for i in list(reversed(range(0,len(nmsg)))):
  for j in range(0, rep):
    print ("smsg={0} nmsg={1}".format(str(smsg[i]), str(nmsg[i])))
    sys.stdout.flush()
    test = subprocess.Popen(["./remote_thr_direct_file","tcp://164.54.143.3:"+str(port), str(smsg[i]), str(nmsg[i]), "/projects/BrainImagingADSP/bicer/gpsf-to-memory/data.tmp"], stdout=subprocess.PIPE).wait()
    #test = subprocess.Popen(["./remote_thr","tcp://164.54.143.3:"+str(port), str(smsg[i]), str(nmsg[i])], stdout=subprocess.PIPE).wait()
    port+=1
    print("End of iteration={0} nmsg={1}".format(j, nmsg[i]))
    sys.stdout.flush()
