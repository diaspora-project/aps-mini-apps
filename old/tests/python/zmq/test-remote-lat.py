import io
import subprocess

#nmsg=(1509949440 ,754974720 ,377487360 ,188743680 ,94371840 ,47185920 ,23592960 ,11796480 ,5898240 ,294912 ,1474560 ,2949120 ,5898240 ,16220160 ,32440320 )
#smsg=(2 ,4 ,8 ,16 ,32 ,64 ,128 ,256 ,512 ,1024 ,2048 ,4096 ,8192 ,22528 ,45056)
nmsg=(2949120, 2949120)
smsg=(1024, 1024)

rep=5
port=5555

for i in range(0,len(nmsg)):
  max_thr=float('-inf')
  min_thr=float('inf')
  avg_sum=0.
  for j in range(0, rep):
    test = subprocess.Popen(["./remote_lat","tcp://164.54.143.3:"+str(port), str(smsg[i]), str(nmsg[i])], stdout=subprocess.PIPE)
    port+=1
    lines = test.communicate()[0].split('\n')
    arr=(smsg[i], nmsg[i], float(lines[2].split()[2]))
    if max_thr<arr[2]:
      max_thr=arr[2]
    if min_thr>arr[2]:
      min_thr=arr[2]
    avg_sum+=arr[2]
    print("{0} {1} {2}".format(arr[0], arr[1], arr[2]))
  print ("Avg/Min/Max: {0} {1} {2}".format(1.0*avg_sum/rep, min_thr, max_thr, smsg[i]))
