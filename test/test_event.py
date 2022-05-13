from skinny_client import SkinnyClient
from conftest import Cluster
from collections import defaultdict
import time


async def test_event_file(cluster: Cluster):
    """
    Test that events for a normal file can be delivered
    """
    a = SkinnyClient()
    counter = defaultdict(int)

    def callback(fh: int):
        counter[fh] += 1

    fh = a.Open("/test", callback)
    a.SetContent(fh, "1")
    a.SetContent(fh, "2")
    time.sleep(1)

    assert counter[fh] == 2


async def test_event_dir(cluster: Cluster):
    """
    Test that events for a directory can be delivered
    """
    a = SkinnyClient()
    counter = defaultdict(int)

    def callback(fh: int):
        counter[fh] += 1

    fruitefh = a.OpenDir("/fruit", callback)
    applefh = a.Open("/fruit/apple", callback)
    a.SetContent(applefh, "fuji")
    bananafh = a.Open("/fruit/banana", callback)
    guavafh = a.Open("/fruit/guava", callback)
    a.Delete(bananafh)
    time.sleep(1)

    assert counter[applefh] == 1
    assert counter[fruitefh] == 4


if __name__ == "__main__":
    import asyncio

    asyncio.run(test_event_file(None))
