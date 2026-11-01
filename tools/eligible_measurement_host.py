"""Windows measurement receipts using the owner's capacity/headroom policy.

Admission/reservation belongs to benchmark_hosts.Pool. This adapter retains the
existing NDS census interface without the old build/game-name or quiet-host
veto. Existing historical receipts are never rewritten.
"""
import json
import os
import re
import subprocess
import time

import benchmark_host_agent as capacity
import measure_host_work as host


def gpu_snapshot():
    script="@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine -ErrorAction Stop | Select-Object Name,UtilizationPercentage)|ConvertTo-Json -Compress"
    wire=subprocess.check_output(['C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe',
        '-NoProfile','-Command',script],text=True,creationflags=subprocess.CREATE_NO_WINDOW)
    rows=json.loads(wire or '[]')
    if isinstance(rows,dict):
        rows=[rows]
    return [dict(pid=int(re.search(r'pid_(\d+)_',r['Name'])[1]),engine=r['Name'],
        utilization=r['UtilizationPercentage']) for r in rows]


def assess(previous,current,excluded,idle_pids,gpu):
    old={r['pid']:r for r in previous['processes']}
    elapsed=current['time']-previous['time']
    if elapsed<=0:
        raise ValueError('nonpositive measurement interval')
    rows=[dict(pid=r['pid'],name=r['name'],cpu=max(0,r['cpu']-old.get(r['pid'],{'cpu':0})['cpu'])) for r in current['processes']]
    background=[r for r in rows if r['pid'] not in excluded]
    cores=sum(r['cpu'] for r in background)/elapsed
    own_cores=sum(r['cpu'] for r in rows if r['pid'] in excluded)/elapsed
    sample=capacity.probe('G:/')
    logical=sample['logical_cpus']
    # Include system/driver activity that process census permissions may miss.
    background_fraction=max(cores/logical,sample['cpu_busy_percent']/100-own_cores/logical,0)
    sample['cpu_free_cores']=sample['physical_cores']*max(0,1-background_fraction)
    sample['cpu_busy_percent']=100*background_fraction
    engines={}
    for row in gpu:
        if row['pid'] not in excluded:
            key=(row['pid'],row['engine'])
            engines[key]=max(engines.get(key,0),row['utilization'])
    grouped={}
    for (_,engine),value in engines.items():
        key=re.sub(r'^pid_\d+_', '', engine)
        grouped[key]=grouped.get(key,0)+value
    if gpu:
        sample['gpu_busy_percent']=min(100,max(grouped.values(),default=0))
        sample['gpu_source']='background-only WDDM aggregate maximum'
    requested=dict(cpu=1.5,gpu=20,memory=2,disk=1)
    decision=capacity.assess(sample,requested)
    return dict(cores=cores,competitors=[],validated_idle=[],gpu=gpu,
        top=sorted(background,key=lambda r:-r['cpu'])[:8],qualified=decision['eligible'],
        logical_cpus=logical,headroom=decision,policy=capacity.FORMAT,
        note='Capacity eligibility only; not proof of a clean A/B speedup. Process names do not veto. Reserve the whole matched pair with benchmark_hosts.Pool.')


def gate(cpu_seconds,excluded,path,idle_pids):
    previous=host.census(cpu_seconds)
    time.sleep(1)
    current=host.census(cpu_seconds)
    receipt=assess(previous,current,excluded,idle_pids,gpu_snapshot())
    path.write_text(json.dumps([receipt],indent=2)+'\n')
    if not receipt['qualified']:
        raise RuntimeError('insufficient measured headroom; rotate to another compatible pool target')
