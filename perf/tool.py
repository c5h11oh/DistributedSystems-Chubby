from conf import *
from os import system
from threading import Thread
import sys

def ssh(node_num, cmd, save_output = False, bg = False):
    # send_cmd = f"ssh -o \"StrictHostKeyChecking no\" node{node_num}{HOST_SUFFIX} \"{cmd}\"" + \
    send_cmd = f"ssh node{node_num}{HOST_SUFFIX} \'( {cmd} )\'" + \
                (f" > output_{node_num}" if save_output else "") + \
                (f" &" if bg else "")
    print(send_cmd)
    system(send_cmd)

def scp(node_num, local_src, remote_dst):
    dst = (":" + remote_dst) if remote_dst else ""
    print(f"scp {local_src} node{node_num}{HOST_SUFFIX}{dst}")
    system(f"scp {local_src} node{node_num}{HOST_SUFFIX}{dst}")

if __name__=="__main__":
    # use it as parallel-ssh
    thd = []
    for node_num in range(TOTAL_NODE_CNT):
        if sys.argv[1] == 'ssh':
            thd.append(Thread(target=ssh, args=[node_num, ' '.join(sys.argv[2:])]))
        else:
            thd.append(Thread(target=scp, args=[node_num, sys.argv[2], sys.argv[3] if sys.argv[3] else ""]))
        thd[-1].start()
    for t in thd:
        t.join()
