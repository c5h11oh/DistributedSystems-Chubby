from skinny_client import SkinnyClient
from conftest import Cluster


async def test_leader_dead(cluster: Cluster):
    """
    Test that the client can still read the previously written
    data after a leader has crashed
    """
    a = SkinnyClient()
    fh = a.Open("/test")
    test_str = b"Hello World!"
    a.SetContent(fh, test_str)
    assert a.GetContent(fh) == test_str
    leader_id = await cluster.kill_leader()
    assert a.GetContent(fh) == test_str
    await cluster.start(leader_id)
    await cluster.kill_leader()
    assert a.GetContent(fh) == test_str
