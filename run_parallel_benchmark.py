#!/usr/bin/env python3
import os
import sys
import itertools
import subprocess
import concurrent.futures

# Folders
DB_BENCH = "./build/db_bench"
DB_FOLDER_BASE = "/nvmepool/andrewk/tmp/rocksdb"
REPORT_FOLDER = "./report-dec10_2/data/"
LOG_FOLDER = "./report-dec10_2/logs/"

# Benchmark Params
NUM = 50_000_000           # 50M keys (~5.8 GB raw @ 16+100B)
CACHE = 12 << 30           # Make working set cache-hot
SEED = 1760593465559160
WARMUP_DURATION = 30
DURATION = 180

STATISTICS = 0
PERF_LEVEL = 1  # kExceptTickers

# Parameter grids (mirror your bash loops; extend as needed)
KEY_SIZES = [16]
VALUE_SIZES = [8, 16, 64, 100]
RESTARTS = [1, 4, 16, 64]
TYPES = ["nohash", "whash", "wphash"]

os.makedirs(REPORT_FOLDER, exist_ok=True)
os.makedirs(LOG_FOLDER, exist_ok=True)


def is_progress_line(line: str) -> bool:
    return line.startswith("... finished") and line.endswith("ops")


def run_capture_progress(cmd, outfile_path, log_path):
    """
    Capture full db_bench output into outfile.
    """

    with open(outfile_path, "w") as fout, open(log_path, "w") as flog:
        p = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        for raw in p.stdout:
            line = raw.strip()
            if is_progress_line(line):
                flog.write(line + "\n")
                flog.flush()
            else:
                fout.write(raw)

        p.wait()

        if p.returncode != 0:
            raise subprocess.CalledProcessError(p.returncode, cmd)

def run_one(config):
    """
    Run write + warmup read + read for a single (key_size, value_size, restart, type) combo.
    """
    key_size, value_size, restart, t = config
    tag = f"k{key_size}-v{value_size}-r{restart}-{t}"

    # Make DB dir unique per config to avoid race conditions when running in parallel.
    db_dir = f"{DB_FOLDER_BASE}-{t}-k{key_size}-v{value_size}-r{restart}"

    out_prefix = os.path.join(
        REPORT_FOLDER,
        f"cache12_key{key_size}_value{value_size}_restart{restart}_{t}"
    )
    log_prefix = os.path.join(
        LOG_FOLDER,
        f"log_key{key_size}_value{value_size}_restart{restart}_{t}"
    )

    # Clean database for this config
    if os.path.exists(db_dir):
        print(f"[{config}] Removing existing DB dir {db_dir}")
        subprocess.run(["rm", "-rf", db_dir], check=True)

    common_args = [
        f"--seed={SEED}",
        f"--db={db_dir}",
        f"--cache_size={CACHE}",
        "--compression_type=none",
    ]

    write_args = [
        DB_BENCH,
        *common_args,
        "--benchmarks=filluniquerandom,waitforcompaction",
        f"--num={NUM}",
        f"--key_size={key_size}",
        f"--value_size={value_size}",
        f"--block_restart_interval={restart}",
        f"--statistics={STATISTICS}",
        f"--perf_level={PERF_LEVEL}",
    ]

    warmup_args = [
        DB_BENCH,
        *common_args,
        "--benchmarks=readrandom",
        "--use_existing_db=1",
        f"--duration={WARMUP_DURATION}",
        "--use_existing_keys=1",
        "--statistics=0",
        "--perf_level=0",
    ]

    read_args = [
        DB_BENCH,
        *common_args,
        "--benchmarks=readrandom",
        "--use_existing_db=1",
        f"--duration={DURATION}",
        "--use_existing_keys=1",
        "--statistics={}".format(STATISTICS),
        "--use_existing_keys=1",
        f"--perf_level={PERF_LEVEL}",
    ]

    # Hash vs perfect hash flags
    if t == "whash":
        write_args.append("--use_data_block_hash_index=1")
        read_args.append("--use_data_block_hash_index=1")
    elif t == "wphash":
        write_args.append("--use_data_block_perfect_hash_index=1")
        read_args.append("--use_data_block_perfect_hash_index=1")

    print(f"[{tag}] Write...")
    run_capture_progress(
        write_args,
        outfile_path=out_prefix + "_write",
        log_path=log_prefix + "_write.log"
    )

    print(f"[{tag}] Warmup...")
    subprocess.run(warmup_args, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, check=True)

    print(f"[{tag}] Read...")
    run_capture_progress(
        read_args,
        outfile_path=out_prefix + "_read",
        log_path=log_prefix + "_read.log"
    )

    print(f"[{tag}] DONE")
    return config


def main():
    # Build the job list
    jobs = list(itertools.product(KEY_SIZES, VALUE_SIZES, RESTARTS, TYPES))
    if not jobs:
        print("No jobs configured.")
        return

    # Choose parallelism; tweak if you want fewer concurrent heavy jobs
    max_workers = 1
    print(f"Running {len(jobs)} jobs with up to {max_workers} workers")

    if max_workers == 1:
        for cfg in jobs:
            try:
                run_one(cfg)
            except subprocess.CalledProcessError as e:
                print(f"[{cfg}] FAILED with return code {e.returncode}")
            except Exception as e:
                print(f"[{cfg}] FAILED with error: {e}")
        print("All jobs completed.")
        return

    with concurrent.futures.ProcessPoolExecutor(max_workers=max_workers) as executor:
        future_to_cfg = {executor.submit(run_one, cfg): cfg for cfg in jobs}
        for future in concurrent.futures.as_completed(future_to_cfg):
            cfg = future_to_cfg[future]
            try:
                future.result()
            except subprocess.CalledProcessError as e:
                print(f"[{cfg}] FAILED with return code {e.returncode}")
            except Exception as e:
                print(f"[{cfg}] FAILED with error: {e}")

    print("All jobs completed.")

if __name__ == "__main__":
    for i in [1, 2, 3]:
        DB_FOLDER_BASE = f"/nvmepool/andrewk/tmp/rocksdb-{i}"
        REPORT_FOLDER = f"./report-dec10_2/data-{i}/"
        LOG_FOLDER = f"./report-dec10_2/logs-{i}/"
        os.makedirs(REPORT_FOLDER, exist_ok=True)
        os.makedirs(LOG_FOLDER, exist_ok=True)
        main()
