from tool import *
from conf import *
import sys

def run(node_num):
    # run
    ssh(node_num, f"LD_LIBRARY_PATH=. ./perf_client {node_num} {NODE_CNT} {sys.argv[1]} {sys.argv[2]} {sys.argv[3]}", save_output=True)


if __name__=="__main__":
    # argv[1:] == [thd_cnt duration write_ratio] 
    if len(sys.argv) != 4:
        print("argv[1:] == [thd_cnt duration write_ratio]")
        exit(1)
    thd = []
    for node_num in range(NODE_CNT):
        thd.append(Thread(target=run, args=[node_num]))
        thd[-1].start()
    for t in thd:
        t.join()