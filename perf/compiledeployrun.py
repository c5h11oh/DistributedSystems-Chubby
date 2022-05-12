from conf import *
from tool import *
import sys

def deployandrun(node_num):
    # deploy
    scp(node_num, BUILD_DIR + "/perf_client " + BUILD_DIR + "/libclientlib.so", "~")

    # run
    ssh(node_num, f"LD_LIBRARY_PATH=. ./perf_client {node_num} {NODE_CNT} {sys.argv[1]} {sys.argv[2]} {sys.argv[3]}", save_output=True)


if __name__=="__main__":
    # argv[1:] == [thd_cnt duration write_ratio] 

    # compile
    system(f"cd {BUILD_DIR} && make -j && cd -")

    # deploy and run    
    thd = []
    for node_num in range(NODE_CNT):
        thd.append(Thread(target=deployandrun, args=[node_num]))
        thd[-1].start()
    for t in thd:
        t.join()