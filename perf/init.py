from conf import *
from tool import *
from threading import Thread

def init(node_num):
    installgxx11 = "sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test; sudo apt update; sudo apt install -y g++-11"
    ssh(node_num, installgxx11)
    # scp(node_num, "~/.ssh/id_ed25519", "~/.ssh/")

if __name__=="__main__":
    thd = []
    for node_num in range(NODE_CNT):
        thd.append(Thread(target=init, args=[node_num]))
        thd[-1].start()
    for t in thd:
        t.join()