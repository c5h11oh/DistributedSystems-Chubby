import random
import string
import time
from build.pyclientlib import SkinnyClient

a = SkinnyClient()
fh = a.Open("codyisgod")
for _1 in range(2):
    for _2 in range(500000):
        a.SetContent(fh, ''.join(random.choices(string.ascii_lowercase, k=6)))
        print(a.GetContent(fh))
    print("sleep")
    time.sleep(10)
    print("wake up")
    
