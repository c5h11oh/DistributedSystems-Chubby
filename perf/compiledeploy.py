from conf import *
from tool import *
import sys

def deploy(node_num):
    # deploy
    if (node_num < CLIENT_NODE_START):
        scp(node_num, SOURCE_BUILD_DIR + "/server ", TARGET_DIR + "sven_server")
    else:
        scp(node_num, SOURCE_BUILD_DIR + "/perf_client " + SOURCE_BUILD_DIR + "/libclientlib.so ", TARGET_DIR)

if __name__=="__main__":
    # compile
    system(f"cd {SOURCE_BUILD_DIR} && make -j && cd -")

    # deploy    
    thd = []
    for node_num in range(SERVER_NODE_START, TOTAL_NODE_CNT):
        thd.append(Thread(target=deploy, args=[node_num]))
        thd[-1].start()
    for t in thd:
        t.join()