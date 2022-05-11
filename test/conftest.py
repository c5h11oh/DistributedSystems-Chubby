import pytest
import asyncssh
import asyncio
import os
import subprocess
import logging
import time
from typing import List
from skinny_client import SkinnyDiagnosticClient
import multiprocessing

server_addrs = [
    "127.0.0.1",
    "127.0.0.1",
    "127.0.0.1",
]

PREFIX = "i_fogot_to_set_prefix_"
BIN_DIR = "debug"


@pytest.fixture(scope="session", autouse=True)
async def scp():
    multiprocessing.set_start_method("spawn")
    asyncssh.set_log_level(100)
    build_dir = os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", BIN_DIR)
    for s in server_addrs:
        subprocess.run(
            [
                "scp",
                "-o",
                "StrictHostKeyChecking=no",
                "-o",
                "UserKnownHostsFile=/dev/null",
                os.path.join(build_dir, "server"),
                f"{s}:/tmp/{PREFIX}server",
            ],
            check=True,
        )
    logging.info("scp completed")


@pytest.fixture(scope="session")
def event_loop():
    return asyncio.get_event_loop()


class Server:
    def __init__(self, conn: asyncssh.SSHClientConnection, node_number):
        self.conn = conn
        self.node_number = node_number
        self.tmux_ses_name = f"{PREFIX}test_{self.node_number}"

    async def start(self):
        await self.conn.run(f"pkill -f '/tmp/{PREFIX}server {self.node_number}'")
        await self.conn.run(f"tmux new-session -d -s {self.tmux_ses_name} 'bash'")
        await self.conn.run(
            f"tmux send-keys -t {self.tmux_ses_name}.1 '/tmp/{PREFIX}server {self.node_number}' ENTER"
        )

    async def close(self):
        await self.conn.run(f"pkill -f '/tmp/{PREFIX}server {self.node_number}'")


class Cluster:
    def __init__(self, servers: List[Server]):
        self.servers: List[Server] = servers
        self.client = SkinnyDiagnosticClient()

    async def kill_leader(self):
        while True:
            leader_id = self.client.GetLeader()
            if leader_id != -1:
                await self.servers[leader_id].close()
                time.sleep(0.5)
                return leader_id

    async def start(self, id: int):
        await self.servers[id].start()
        time.sleep(0.5)


@pytest.fixture
async def cluster():
    servs = []
    for idx, node in enumerate(server_addrs):
        conn = await asyncssh.connect(node, known_hosts=None)
        serv = Server(conn, idx)
        await serv.start()
        servs.append(serv)
    ret = Cluster(servs)
    time.sleep(2)  # wait for server to start
    yield ret
    for c in servs:
        await c.close()
    time.sleep(2)
