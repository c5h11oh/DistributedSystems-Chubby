from time import sleep
import run

CLIENT_CNT=5
THREAD=30
DURATION=30
# for WRITE_RATIO in ["0.8", "0.9", "1"]:
for WRITE_RATIO in ["0.1", "0.2", "0.3", "0.4", "0.5", "0.6", "0.7", "0.8", "0.9", "1"]:
    run.run_exp(CLIENT_CNT, THREAD, DURATION, WRITE_RATIO)
    sleep(3)
