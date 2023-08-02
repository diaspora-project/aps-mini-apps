docker build --no-cache -f Dockerfile.StreamerSIRT -t streamer-sirt:latest --target StreamerSIRT .
docker run -d --rm --name streamer-sirt -p 52000:52000 streamer-sirt:latest 
docker exec /Trace/build/bin/sirt_stream --write-freq 4 --dest-host host.docker.internal --dest-port 50010 --window-iter 1 --window-step 4 --window-length 4 -t 2 -c 1427 --pub-addr tcp://*:52000