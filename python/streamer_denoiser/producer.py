import os
import sys
import json
import time
from pymargo.core import Engine
import mochi.mofka.client as mofka

mofka_protocol = "na+sm"
group_file = "/home/agueroudji/tekin-aps-mini-apps/build/mofka.json"


engine = Engine(mofka_protocol)
client = mofka.Client(engine)
service = client.connect(group_file)

# create a topic
topic_name = "mytopic"
service.create_topic(topic_name)
service.add_memory_partition(topic_name, 0)
topic = service.open_topic(topic_name)
producer_name = "producer"
batchsize = mofka.AdaptiveBatchSize
thread_pool = mofka.ThreadPool(1)
ordering = mofka.Ordering.Strict
producer = topic.producer(producer_name, batchsize, thread_pool, ordering)

f = producer.push({"action": "restart"}, "restart".encode("utf-8"))
f.wait()
