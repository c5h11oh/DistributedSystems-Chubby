import logging
import time
import string
import random
from skinny_client import SkinnyClient
from conftest import Cluster
import threading

async def test_lock_release(cluster: Cluster):
    a = SkinnyClient()
    b = SkinnyClient()
    afh = a.Open("/test")
    bfh = b.Open("/test")
    a.Acquire(afh, True)
    a.Release(bfh)
    b.Acquire(afh, True)

async def test_concurrent_lock_release(cluster: Cluster):
    a = SkinnyClient()
    fh = a.Open("/test")
    assert a.TryAcquire(fh, True) == True

    NUM_THREADS = 200
    barrier = threading.Barrier(NUM_THREADS + 1)
    barrier2 = threading.Barrier(NUM_THREADS)
    get_lock_counter = 0
    def try_acquire():
        nonlocal get_lock_counter
        a = SkinnyClient()
        fh = a.Open("/test")
        barrier.wait()
        success = a.TryAcquire(fh, True)
        if success:
            get_lock_counter += 1
        barrier2.wait()

    threads = [threading.Thread(target=try_acquire) for _ in range(NUM_THREADS)]
    for i in threads:
        i.start()
    barrier.wait()
    a.Release(fh)
    success = a.TryAcquire(fh, True)
    if success:
        get_lock_counter += 1
    for i in threads:
        i.join()
    assert get_lock_counter == 1
