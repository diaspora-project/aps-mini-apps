docker build -f Dockerfile.StreamerSIRT -t streamer-sirt:latest --target StreamerSIRT .
docker run -it --rm --name streamer-sirt -p 52000:52000 streamer-sirt:latest 

./sirt_stream --dest-host host.docker.internal --dest-port 50010 --window-iter 1 --window-step 4 --window-length 4 -t 2 -c 1427 --pub-addr tcp://*:52000
