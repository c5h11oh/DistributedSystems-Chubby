# fmt: off
import sys
sys.path.append('../build')
from time import sleep
from multiprocessing import Barrier, Process
from pyclientlib import SkinnyClient
# fmt: on

NUM_PROC = 200
barrier = Barrier(NUM_PROC)


def client1(proc_no):
    skinny = SkinnyClient()
    fh = skinny.Open("blocked_lock_req_file")
    barrier.wait()
    success = skinny.Acquire(fh, True)  # contend the exclusive lock
    if success:
        print(f"{proc_no:3d}: lock acquired")
    else:
        print(f"{proc_no:3d} error: Acquire failed")
        exit(1)
    tmp_file = []
    for i in range(10):
        filename = 'tmp_file' + str(i + 1)
        tmp_file.append(skinny.Open(filename))
        print(f"{proc_no:3d} opened ")


if __name__ == '__main__':
    proc = []
    for proc_no in range(NUM_PROC):
        proc.append(Process(target=f, args=[proc_no]))
        proc[-1].start()
    for p in proc:
        p.join()
    print("bye")
