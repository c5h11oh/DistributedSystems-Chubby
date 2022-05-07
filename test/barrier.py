# fmt: off
import sys
sys.path.append('../build')
from time import sleep
from multiprocessing import Barrier, Process
from pyclientlib import SkinnyClient
# fmt: on

NUM_PROC = 2
barrier = Barrier(NUM_PROC)


def f(proc_no):
    print(f"start of f {proc_no}")
    skinny = SkinnyClient()
    fh = skinny.Open("master_file")
    barrier.wait()
    success = skinny.TryAcquire(fh, True)  # contend the exclusive lock
    if success:
        skinny.SetContent(fh, str(proc_no))
        print(f"{proc_no:2d}: lock acquired!")
    else:
        print(f"{proc_no:2d}: lost. master is {int(skinny.GetContent(fh)):2d}")
    sleep(0.5)


if __name__ == '__main__':
    proc = []
    for proc_no in range(NUM_PROC):
        proc.append(Process(target=f, args=[proc_no]))
        proc[-1].start()
    for p in proc:
        p.join()
    print("bye")
