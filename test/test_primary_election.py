from skinny_client import SkinnyClient
from conftest import Cluster
from time import sleep
from threading import Barrier, Thread, Event, Lock
import logging
import logging
import sys

# logging.basicConfig(stream=sys.stdout, level=logging.DEBUG)

NUM_PROC = 1000
barrier = Barrier(NUM_PROC)
barrier2 = Barrier(NUM_PROC)
leader_written = Event()
lock = Lock()

whoiselected = [-1] * NUM_PROC


def f(proc_no):
    
    skinny = SkinnyClient()
    def callback(fh):
        leader = int(skinny.GetContent(fh))
        logging.info(f"{proc_no:2d}: leader is {leader}!")
        lock.acquire()
        whoiselected[proc_no] = leader
        lock.release()

    fh = skinny.Open("/primary_file", callback)
    barrier.wait()
    success = skinny.TryAcquire(fh, True)  # contend the exclusive lock
    if success:
        skinny.SetContent(fh, str(proc_no))
        logging.info(f"{proc_no:2d}: lock acquired!")
    barrier2.wait()


async def test_primary_election(cluster: Cluster):
    proc = []
    for proc_no in range(NUM_PROC):
        proc.append(Thread(target=f, args=[proc_no]))
        proc[-1].start()
    for p in proc:
        p.join()
    print(whoiselected)
    for i in whoiselected:
        assert i != -1
        assert i == whoiselected[0]


if __name__ == "__main__":
    import asyncio

    asyncio.run(test_primary_election(None))
