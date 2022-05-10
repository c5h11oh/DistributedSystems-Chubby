from pickle import TRUE
from skinny_client import SkinnyClient
from conftest import Cluster
from collections import defaultdict
import logging


async def test_client_dead(cluster: Cluster):
    a = SkinnyClient()
    b = SkinnyClient()
    a.TEST_set_no_implicit_end_session_on_destruct(True)
    afh = a.Open("/test")
    bfh = b.Open("/test")
    a.SetContent(afh, "abc")
    del a
    assert b.GetContent(bfh) == b"abc"
    b.SetContent(bfh, "efg")
    assert b.GetContent(bfh) == b"efg"
