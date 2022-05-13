from time import sleep
from tool import *
from conf import *
import sys

def run_client(node_num, client_cnt, thd_cnt, dur, write_ratio, lat = False):
    # if (node_num >= SERVER_NODE_START and node_num < CLIENT_NODE_START):
    #     # run server
    #     ssh(node_num, f"{TARGET_DIR}/server {node_num}", save_output=True)
    if (node_num >= CLIENT_NODE_START):
        # run client
        ssh(node_num, f"LD_LIBRARY_PATH={TARGET_DIR} {TARGET_DIR}/{'lat' if lat else 'perf'}_client {node_num} {client_cnt} {thd_cnt} {dur} {write_ratio}", save_output=True)

def run_exp(client_cnt, thd_cnt, dur, write_ratio, lat =  False):
    for server in range(SERVER_NODE_START, CLIENT_NODE_START):
        ssh(server, f"pkill -e sven_server")
    sleep(0.5)
    for server in range(SERVER_NODE_START, CLIENT_NODE_START):
        ssh(server, f"tmux send-keys -t {TMUX_SES_NAME}.0 {TARGET_DIR}sven_server Space {server} ENTER")
    sleep(3)
    thd = []
    for node_num in range(CLIENT_NODE_START, CLIENT_NODE_START + int(client_cnt)):
        thd.append(Thread(target=run_client, args=[node_num, client_cnt, thd_cnt, dur, write_ratio, lat]))
        thd[-1].start()
    for i in range(int(client_cnt)):
        thd[i].join()

    # collect result
    server_cnt = CLIENT_NODE_START - SERVER_NODE_START
    sum_read_ops = 0
    sum_write_ops = 0
    avg_read_lat = 0
    avg_write_lat = 0
    p99_read_lat = 0
    p99_write_lat = 0

    for node_num in range(CLIENT_NODE_START, CLIENT_NODE_START + int(client_cnt)):
        with open(f'output_{node_num}', 'r') as f:
            res = f.read().split(',')
            sum_read_ops += int(res[0].split("=")[1])
            sum_write_ops += int(res[1].split("=")[1])
            avg_read_lat += int(res[2].split("=")[1]) * int(res[0].split("=")[1])
            avg_write_lat += int(res[3].split("=")[1]) * int(res[1].split("=")[1])
            if (node_num == CLIENT_NODE_START):
                p99_read_lat = int(res[4].split("=")[1])
                p99_write_lat = int(res[5].split("=")[1])
    if sum_read_ops != 0:
        avg_read_lat //= sum_read_ops
    if sum_write_ops != 0:
        avg_write_lat //= sum_write_ops
    with open(f'{"lat" if lat else "exp"}_output', 'a') as f:
        f.write(f'{server_cnt}\t{client_cnt}\t{thd_cnt}\t{dur}\t{write_ratio}\t{sum_read_ops}\t{sum_write_ops}')
        if (lat):
            f.write(f'\t{avg_read_lat}\t{avg_write_lat}\t{p99_read_lat}\t{p99_write_lat}')
        f.write("\n")

if __name__=="__main__":
    # argv[1:] == [client_cnt thd_cnt duration write_ratio] 
    if len(sys.argv) != 5:
        print("argv[1:] == [client_cnt thd_cnt duration write_ratio]")
        exit(1)
    run_exp(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])