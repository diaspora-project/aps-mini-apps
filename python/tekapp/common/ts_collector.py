import time

class TimestampCollector:
    def __init__(self):
        self._entries = []

    def record(self, line: str):
        ts = time.time_ns() // 1000
        self._entries.append(f"{ts} {line}")

    def write(self, filename: str):
        with open(filename, 'w') as f:
            for entry in self._entries:
                f.write(entry + '\n')
