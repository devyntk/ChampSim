#!/usr/bin/env python3
import json
import sys
import os
from pathlib import Path
import math
import subprocess

instr_flags = "--warmup_instructions 200000 --simulation_instructions 500000"
scheds=("fcfs", "frfcfs", "atlas", "bliss")
num_cores = 4
percent_mem = 0.50

A_mem = (436, 459, 459, 470, 437, 437, 429, 433, 450, 450, 483, 483)
A_non = (473, 401, 454, 454, 454, 447, 445, 445, 471, 471, 458, 465)
A = (A_mem, A_non, "A")

B_mem = (459, 470, 437, 462, 429, 429, 433, 450, 450, 450, 482, 482)
B_non = (473, 473, 473, 401, 401, 454, 454, 447, 456, 444, 453, 481)
B = (B_mem, B_non, "B")

C_mem = (436, 459, 459, 470, 470, 470, 429, 433, 433, 450, 450, 482)
C_non = (401, 401, 447, 447, 403, 403, 464, 456, 400, 458, 465, 465)
C = (C_mem, C_non, "C")

D_mem = (459, 459, 459, 470, 437, 462, 433, 450, 482, 482, 482, 483)
D_non = (401, 447, 454, 445, 435, 464, 464, 456, 400, 453, 458, 481)
D = (D_mem, D_non, "D")

workloads = (A, B, C, D)


def get_file_from_short(short):
    p = Path('../')
    files = list(p.glob(f"./traces/{short}.*.champsimtrace.xz"))
    if len(files) == 0 or len(files) > 1:
        print(f"Could not find trace for short code {short}")
        sys.exit(1)
    return str(files[0])

end_results =[]
for scheduler in scheds:
    print(f"Compiling and testing scheduler {scheduler}")
    os.system("cd ../ ; make clean")
    with open(sys.argv[1]) as rfp:
        config_file = json.load(rfp)
    bin_name = f"bin/{scheduler}_champsim"
    config_file["executable_name"] = bin_name
    config_file['physical_memory']['scheduler'] = scheduler
    with open('modified.json', 'wt') as wfp:
        json.dump(config_file, wfp)
    os.system("cd ../ ; ./config.py ./scripts/modified.json")
    os.system("cd ../ ; make")
    mem_take_num = math.floor(num_cores * percent_mem)
    non_take_num = num_cores - mem_take_num
    for workload in workloads:
        mem_workloads = workload[0][0:mem_take_num]
        non_workloads = workload[1][0:non_take_num]
        total_workloads = mem_workloads + non_workloads
        workloads_files = [get_file_from_short(short) for short in total_workloads]
        cwd = Path('../bin').resolve()
        print(f"Starting execution of {scheduler} with workload {workload[2]}")
        ret = subprocess.run(f"./{scheduler}_champsim {instr_flags} {' '.join(workloads_files)}",
                             capture_output=True, cwd=cwd, shell=True)
        filename = f"../results/{scheduler}/{workload[2]}.out"
        os.makedirs(os.path.dirname(filename), exist_ok=True)
        with open(filename, "wb") as f:
            f.write(ret.stdout)
            f.write(ret.stderr)
        for line in ret.stdout.splitlines():
            if "all-core cumulative IPC:" in str(line):
                print(line)
                end_results += (f"{scheduler} {workload[2]} IPC: {line}",)
print('\n'.join(end_results))
