from skinny_client import SkinnyClient
from conftest import Cluster
import logging
import multiprocessing


def clientA(event, no):
    a = SkinnyClient()
    afh = a.Open("/test")
    a.SetContent(afh, "abc")
    event.set()
    no.wait()


async def test_client_dead(cluster: Cluster):
    """
    Test that a SetContent call will not be blocked indefinitely
    when another client that had opened the same file before
    crashed. (Because SetContent should wait until all clients respond
    to a cache invalidate request in the normal case)
    """

    event = multiprocessing.Event()
    no = multiprocessing.Event()
    p = multiprocessing.Process(target=clientA, args=[event, no])
    p.start()
    event.wait()
    b = SkinnyClient()
    bfh = b.Open("/test")
    assert b.GetContent(bfh) == b"abc"
    p.terminate()
    b.SetContent(bfh, "efg")
    assert b.GetContent(bfh) == b"efg"


if __name__ == "__main__":
    import asyncio

    asyncio.run(test_client_dead(None))
