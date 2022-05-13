import logging
import time
import string
import random
from skinny_client import SkinnyClient
import threading


def test_many_writes(cluster):
    """
    Test simple read write
    """
    a = SkinnyClient()
    fh = a.Open("/test")
    for _ in range(2):
        for _ in range(100):
            s = "".join(random.choices(string.ascii_lowercase, k=6))
            a.SetContent(fh, s)
            assert a.GetContent(fh).decode() == s
            logging.info(a.GetContent(fh))
        logging.info("sleep")
        time.sleep(1)
        logging.info("wake up")


async def test_concurrent_read(cluster):
    """
    Test reading from 1000 clients concurrently
    """
    a = SkinnyClient()
    fh = a.Open("/test")
    a.SetContent(fh, "abc")

    def child(tid):
        try:
            c = SkinnyClient()
            fh = c.Open("/test")
            assert c.GetContent(fh) == b"abc"
        except RuntimeError:
            logging.info("Session expired")

    NUM_THREADS = 1000
    threads = [threading.Thread(target=child, args=[i]) for i in range(NUM_THREADS)]
    for i in threads:
        i.start()

    for i in threads:
        i.join()


if __name__ == "__main__":
    import asyncio

    asyncio.run(test_concurrent_read(None))
