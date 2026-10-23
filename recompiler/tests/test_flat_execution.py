"""Compile flat C, then compare its guest outcomes with the independent IR interpreter."""
from pathlib import Path
import argparse,hashlib,re,subprocess

p=argparse.ArgumentParser()
for name in ['recompiler','compiler','core_library','runtime_include','core_include','work']:
    p.add_argument('--'+name.replace('_','-'),required=True)
a=p.parse_args();work=Path(a.work);work.mkdir(parents=True,exist_ok=True)
base=0x02010000
words=[0xE1A00000]*256
root=[0xE92D4000,0xE5940004,0xE5941008,0xEB00000B,0xE10F2000,0xE5840000,0xE8BD8000]
words[:len(root)]=root
words[16:20]=[0xE92D4000,0xE0900001,0xE12FFF36,0xE8BD8000]
arm_ops=[0xE1F00000,0xE0B00001,0xE0D00001,0xE0F00001,0xE0500001,
    0xE0700001,0xE0100001,0xE0300001,0xE1900001,0xE1D00001,
    0xE1B00110,0xE1B00130,0xE1B00150,0xE1B00170,0xE0100190,
    0xE16F0F10,0xE1010050,0x12800001,0xE1D400F4,0xE1D400D4,
    0xE1D400B4,0xE1C400B0,0xE1040091,0xE1440091]
entries=[]
for i,op in enumerate(arm_ops):
    offset=0x60+8*i;words[offset//4:offset//4+2]=[op,0xE12FFF1E]
    entries.append((base+offset,'arm',8))
thumb_ops=[0x4088,0x40C8,0x4108,0x4148,0x4188,0x41C8,0x4348]
for i,op in enumerate(thumb_ops):
    offset=0x200+4*i;words[offset//4]=op|(0x4770<<16)
    entries.append((base+offset,'thumb',4))
image=b''.join(w.to_bytes(4,'little') for w in words)
(work/'fixture.bin').write_bytes(image)
(work/'fixture.toml').write_text(f'''[program]
name="flat semantic fixture"
id="flat_fixture"
load_address={base}
size={len(image)}
entry_pc={base}
authoritative_entry_points=true
[identity]
sha1="{hashlib.sha1(image).hexdigest()}"
[[entry_point]]
addr={base}
mode="arm"
name="root"
size=28
[[entry_point]]
addr={base+64}
mode="arm"
name="callee"
size=16
[[entry_point]]
addr={base+76}
mode="arm"
name="callee_return"
size=4
''')
with (work/'fixture.toml').open('a') as config:
    for i,(addr,mode,size) in enumerate(entries):
        config.write(f'[[entry_point]]\naddr={addr}\nmode="{mode}"\nname="kernel_{i}"\nsize={size}\n')
(work/'kernels.inc').write_text(','.join(str(addr|(mode=='thumb'))+'u' for addr,mode,size in entries))
subprocess.run([a.recompiler,'--config',str(work/'fixture.toml'),'--bin',str(work/'fixture.bin'),
    '--out',str(work),'--bank','flat_fixture','--validate-live-bytes','--flat-execution'],check=True)
header=(Path(a.runtime_include)/'runtime_arm.h').read_text()
definitions={}
for m in re.finditer(r'^extern\s+([^;\n]+);',header,re.M):
    declaration=m[1].strip()
    if '(' in declaration or 'g_nds_active' in declaration:continue
    name=re.search(r'(\w+)\s*(?:\[[^]]*\])?$',declaration)
    if name:definitions[name[1]]=declaration+' = {};'
stub='\n'.join(definitions.values())+'\n'
(work/'globals.inc').write_text(stub)
(work/'fixture_image.inc').write_text(','.join(str(x) for x in image))
includes=['-I'+a.runtime_include,'-I'+a.core_include,'-I'+str(work)]
for name in ['flat_fixture.c','flat_fixture_dispatch.c']:
    subprocess.run([a.compiler,'-x','c','-std=gnu11','-O2','-DNDS_STATIC_CPU=0',*includes,
        '-c',str(work/name),'-o',str(work/(name+'.o'))],check=True)
executable=work/'flat_execution_probe.exe'
subprocess.run([a.compiler,'-std=c++20','-O2','-DNDS_STATIC_CPU=0',*includes,
    str(Path(__file__).with_name('flat_execution_probe.cpp')),
    str(work/'flat_fixture.c.o'),str(work/'flat_fixture_dispatch.c.o'),
    a.core_library,'-o',str(executable)],check=True)
subprocess.run([str(executable)],check=True,cwd=work)
