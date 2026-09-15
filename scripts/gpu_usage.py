#!/usr/bin/env python3

import time
import operator

def get_stats():
  try:
    with open('/sys/kernel/debug/dri/0/gpu_usage') as f:
      lines = f.readlines()
  except:
    print("gpu_usage is not supported on this device")
    exit(1)
  queue = {}
  for i in lines:
    s = i.replace("'", "").replace('"', "").strip().rstrip(';').split(';')
    queues = ["bin", "render", "tfu", "csd", "cache_clean"]
    if s[0] == 'timestamp':
        timestamp = int(s[1])
    elif s[0] == 'QUEUE':
        names = s[1:]
    elif s[0].replace("v3d_", "") in queues:
        queue[s[0]] = list(map(int, s[1:]))
    else:
        print(s)
        assert(False)
  return timestamp, names, queue

last_timestamp = None

while True:
  timestamp, names, queue = get_stats()
  if last_timestamp is not None:
    q = {}
    t = timestamp - last_timestamp
    for k,v in queue.items():
      q[k] = list(map(operator.sub, queue[k], last_queue[k]))
    s = []
    for k,v in q.items():
      s.append(f"{k}: jobs:{v[0]:3}{100.0*v[1]/t:6.1f}%")
    print(", ".join(s))
  last_timestamp = timestamp
  last_queue = queue
  time.sleep(1)
