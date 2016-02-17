import subprocess
import argparse

binary = "/Users/bicer/Projects/Trace/src/sirt/sirt_model"
projections = [128, 256, 512, 1024]
slices = [1, 2, 4, 8]
columns = [128, 256, 512, 1024]
iterations = [1, 2]
threads = [1, 2, 4]

for p in projections:
  for s in slices:
    for c in columns:
      for i in iterations:
        for t in threads:
          cmd = [binary, "-p", str(p), "-s", str(s), "-c", str(c), "-i", str(i), "-t", str(t)]
          proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
          output, err = proc.communicate()
          print output
