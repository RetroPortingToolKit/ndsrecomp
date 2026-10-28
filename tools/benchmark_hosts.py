"""Choose/reserve local PC, SERVER or Steam Deck using measured headroom.

One A/B pair stays inside one lease on one compatible physical host. Other
work is allowed: capacity claims, measured CPU/GPU/RAM and storage decide.
No process-name veto, machine-wide quiet lock or remote package install.
"""
import argparse
import base64
import json
import os
from pathlib import Path
import queue
import shlex
import subprocess
import sys
import threading

HERE = Path(__file__).resolve().parent
SSH = 'C:/Windows/System32/OpenSSH/ssh.exe' if os.name == 'nt' else 'ssh'


def ps_quote(value):
    return "'"+str(value).replace("'", "''")+"'"


def transport(host, argv, alias=None, env=None):
    env = env or {}
    if host['transport'] == 'local':
        return argv
    if host['os'] == 'windows':
        # PowerShell5's native argument binder strips embedded double quotes.
        # Transfer exact argv/env as base64 and let Python call CreateProcess.
        payload=base64.b64encode(json.dumps(dict(argv=argv,env=env)).encode()).decode()
        bootstrap="import base64,json,os,subprocess,sys;p=json.loads(base64.b64decode('"+payload+"'));sys.exit(subprocess.call(p['argv'],env={**os.environ,**p['env']},stdin=sys.stdin,stdout=sys.stdout,stderr=sys.stderr,creationflags=subprocess.CREATE_NO_WINDOW))"
        script='& '+ps_quote(host['python'])+' -u -c '+ps_quote(bootstrap)+';exit $LASTEXITCODE'
        encoded = base64.b64encode(script.encode('utf-16le')).decode()
        remote = 'powershell -NoProfile -NonInteractive -EncodedCommand '+encoded
    else:
        remote = shlex.join((['env']+[key+'='+value for key,value in env.items()] if env else [])+argv)
    return [SSH, '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=6', alias, remote]


def read_json(process, timeout=25):
    ready = queue.Queue()
    def read():
        ready.put(process.stdout.readline())
    threading.Thread(target=read, daemon=True).start()
    try:
        line = ready.get(timeout=timeout)
    except queue.Empty:
        raise RuntimeError('host agent response timed out')
    if not line:
        detail = process.stderr.read() if process.poll() is not None else ''
        raise RuntimeError('host agent closed: '+detail.strip()[:700])
    return json.loads(line)


class Lease:
    def __init__(self, process, host, alias, receipt):
        self.process, self.host, self.alias, self.receipt = process,host,alias,receipt

    def request(self, cmd):
        self.process.stdin.write(json.dumps(dict(cmd=cmd))+'\n')
        self.process.stdin.flush()
        return read_json(self.process)

    def running(self):
        return self.request('running')

    def probe(self):
        return self.request('probe')

    def close(self):
        if self.process.poll() is None:
            try:
                self.process.stdin.write('{"cmd":"release"}\n')
                self.process.stdin.flush()
                self.process.stdin.close()
                self.process.wait(timeout=12)
            except (OSError, subprocess.TimeoutExpired):
                self.process.terminate()
                self.process.wait(timeout=5)
        for stream in (self.process.stdout,self.process.stderr):
            stream.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class Pool:
    def __init__(self, config=None):
        self.config = Path(config) if config else HERE/'benchmark_hosts.json'
        self.hosts = json.loads(self.config.read_text())['hosts']
        self.agent = (HERE/'benchmark_host_agent.py').read_text()

    def connect(self, host, requested, owner, reserve=False):
        errors = []
        for alias in host.get('aliases',[None]):
            code = "import base64,sys;exec(base64.b64decode(sys.stdin.buffer.readline()))"
            leased = False
            argv = [sys.executable if host['transport']=='local' else host['python'], '-u', '-c', code,
                '--mode', 'reserve' if reserve else 'probe', '--root', host['root'], '--storage',host['storage'],
                '--requested',base64.b64encode(json.dumps(requested,separators=(',',':')).encode()).decode(), '--owner',owner]
            process = subprocess.Popen(transport(host,argv,alias), stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            try:
                process.stdin.write(base64.b64encode(self.agent.encode()).decode()+'\n')
                process.stdin.flush()
                receipt = read_json(process)
                receipt.update(target=host['id'],alias=alias)
                if reserve and receipt.get('eligible') and receipt.get('token'):
                    leased = True
                    return Lease(process,host,alias,receipt)
                process.stdin.close()
                process.wait(timeout=12)
                return receipt
            except Exception as error:
                errors.append(str(error))
            finally:
                if not leased:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=5)
                    if not process.stdin.closed:
                        process.stdin.close()
                    process.stdout.close()
                    process.stderr.close()
        return dict(target=host['id'],eligible=False,errors=errors)

    def candidates(self, target='auto', system='any'):
        hosts = [h for h in self.hosts if (target=='auto' or h['id']==target) and (system=='any' or h['os']==system)]
        if not hosts:
            raise ValueError('No configured compatible target')
        # Round-robin starting point; actual leases are accounted on each host.
        state = Path(os.environ.get('LOCALAPPDATA',str(Path.home()/'.local/state')))/'codex-benchmark-hosts'
        state.mkdir(parents=True,exist_ok=True)
        cursor_path = state/'rotation.json'
        try:
            cursor = int(json.loads(cursor_path.read_text())['cursor'])
        except (OSError,ValueError,KeyError):
            cursor = 0
        if target=='auto':
            offset=cursor%len(hosts)
            hosts=hosts[offset:]+hosts[:offset]
            cursor_path.write_text(json.dumps(dict(cursor=cursor+1)))
        return hosts

    def reserve(self, requested, owner, target='auto', system='any'):
        receipts=[]
        for host in self.candidates(target,system):
            result=self.connect(host,requested,owner,reserve=True)
            if isinstance(result,Lease):
                result.receipt['other_targets']=receipts
                return result
            receipts.append(result)
        raise RuntimeError('No compatible host has the requested headroom: '+json.dumps(receipts))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=('probe','run'))
    p.add_argument('--config',type=Path)
    p.add_argument('--host',default='auto',choices=('auto','local','server','steamdeck'))
    p.add_argument('--os',default='any',choices=('any','windows','linux'))
    p.add_argument('--cpu',type=float,default=1.5,help='Physical core equivalents required')
    p.add_argument('--gpu',type=float,default=20,help='GPU capacity percent required;0 only for CPU-only work')
    p.add_argument('--memory',type=float,default=2,help='RAM GiB required')
    p.add_argument('--disk',type=float,default=1,help='Workspace GiB required')
    p.add_argument('--owner',default='codex')
    p.add_argument('--out',type=Path)
    p.add_argument('--env',action='append',default=[])
    p.add_argument('--job-file',type=Path,help='JSON argv/env; preserves exact arguments through Windows shells')
    wire=sys.argv[1:]
    split=wire.index('--') if '--' in wire else len(wire)
    a=p.parse_args(wire[:split])
    a.command=wire[split:]
    pool=Pool(a.config)
    requested=dict(cpu=a.cpu,gpu=a.gpu,memory=a.memory,disk=a.disk)
    if a.action=='probe':
        result=[pool.connect(h,requested,a.owner) for h in pool.candidates(a.host,a.os)]
        print(json.dumps(result,indent=2))
        if a.out:
            a.out.write_text(json.dumps(result,indent=2)+'\n')
        return 0
    job=json.loads(a.job_file.read_text()) if a.job_file else {}
    if job:
        if a.command:
            p.error('choose --job-file or -- executable arguments')
        if not isinstance(job.get('argv'),list) or not job['argv'] or not all(isinstance(v,str) for v in job['argv']):
            p.error('job-file requires a nonempty string argv list')
        a.command=['--']+job['argv']
    if not a.command or a.command[0]!='--' or len(a.command)<2:
        p.error('run requires --job-file or -- executable arguments')
    env={**job.get('env',{}),**dict(value.split('=',1) for value in a.env)}
    if any(not key.replace('_','').isalnum() or key[0].isdigit() for key in env):
        p.error('invalid environment variable name')
    with pool.reserve(requested,a.owner,a.host,a.os) as lease:
        lease.running()
        env.update(CODEX_BENCHMARK_LEASE_TOKEN=lease.receipt['token'],CODEX_BENCHMARK_TARGET=lease.host['id'])
        receipt=dict(before=lease.receipt,command=a.command[1:])
        print(json.dumps(lease.receipt),flush=True)
        try:
            result=subprocess.run(transport(lease.host,a.command[1:],lease.alias,env),
                env={**os.environ,**env} if lease.host['transport']=='local' else None,
                stdin=sys.stdin,stdout=sys.stdout,stderr=sys.stderr,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            receipt['exit_code']=result.returncode
            receipt['after']=lease.probe()
        finally:
            if a.out:
                a.out.write_text(json.dumps(receipt,indent=2)+'\n')
        return result.returncode


if __name__=='__main__':
    sys.exit(main())
