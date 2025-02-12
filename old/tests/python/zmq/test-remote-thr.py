import io
import subprocess

GB=1024*1024*1024
nmsg=(10*GB/(8192*2), 10*GB/(8192*4), 10*GB/(8192*8))
smsg=((8192*2), (8192*4), (8192*8))

rep=6
port=5555

for i in range(0,len(nmsg)):
  for j in range(0, rep):
    print ("smsg={0} nmsg={1}".format(str(smsg[i]), str(nmsg[i])))
    test = subprocess.Popen(["./remote_thr","tcp://164.54.143.3:"+str(port), str(smsg[i]), str(nmsg[i])], stdout=subprocess.PIPE).wait()
    port+=1
    print("End of iteration={} nmsg={}".format(j, nmsg[i]))
