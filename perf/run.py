from tool import *
from conf import *
import sys

def run(node_num):
    if (node_num >= SERVER_NODE_START and node_num < CLIENT_NODE_START):
        # run server
        ssh(node_num, f"{TARGET_DIR}/server {node_num}", save_output=True)
    if (node_num >= CLIENT_NODE_START):
        # run client
        ssh(node_num, f"LD_LIBRARY_PATH={TARGET_DIR} {TARGET_DIR}/perf_client {node_num} {TOTAL_NODE_CNT - CLIENT_NODE_START} {sys.argv[1]} {sys.argv[2]} {sys.argv[3]}", save_output=True)



if __name__=="__main__":
    # argv[1:] == [thd_cnt duration write_ratio] 
    if len(sys.argv) != 4:
        print("argv[1:] == [thd_cnt duration write_ratio]")
        exit(1)
    thd = []
    for node_num in range(TOTAL_NODE_CNT):
        thd.append(Thread(target=run, args=[node_num]))
        thd[-1].start()
    for i in range(CLIENT_NODE_START, TOTAL_NODE_CNT):
        thd[i].join()
    exit(0)