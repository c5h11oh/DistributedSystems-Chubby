# import skinny_client
import logging
import time
import string
import random
from skinny_client import SkinnyClient


def test_many_writes():
    a = SkinnyClient()
    fh = a.Open("/test")
    for _ in range(2):
        for _ in range(100):
            s = ''.join(random.choices(string.ascii_lowercase, k=6))
            a.SetContent(fh, s)
            assert a.GetContent(fh).decode() == s
            logging.info(a.GetContent(fh))
        logging.info("sleep")
        time.sleep(1)
        logging.info("wake up")
