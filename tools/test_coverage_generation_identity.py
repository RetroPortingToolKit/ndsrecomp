#!/usr/bin/env python3
"""Exercise the real ingestion CLI with overlapping code bodies, not a mock map."""
from __future__ import annotations
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib

TOOL = Path(__file__).with_name('ingest_coverage_manifests.py')
ROM = '1' * 40
PAGE = 0x02100000
SIZE = 4096

def check(ok, message):
    if not ok:
        raise AssertionError(message)
    print('ok: ' + message)

def bitmap(addresses, stride, base=PAGE):
    data = bytearray(SIZE // stride // 8)
    for address in addresses:
        bit = (address - base) // stride
        data[bit // 8] |= 1 << (bit % 8)
    return base64.b64encode(data).decode()

def entry(offset, kind='call', mode='arm', hits=7, **extra):
    return dict(addr=f'0x{PAGE+offset:08X}', kind=kind, mode=mode, hits=hits, **extra)

def page(blob, entries=(), roots=(), thumb=(), cpu=9, root_hits=16, base=PAGE):
    return dict(cpu=cpu, addr=f'0x{base:08X}', sha1=hashlib.sha1(blob).hexdigest(),
                data=base64.b64encode(blob).decode(), entry_points=list(entries),
                root_arm=bitmap(roots,4,base), root_thumb=bitmap(thumb,2,base),
                root_hits=[root_hits]*16)

def manifest(path, pages, entries=(), roots=(), schema=4):
    path.write_text(json.dumps(dict(kind='ndsrecomp-tier3-coverage',schema=schema,
        rom_sha1=ROM,build_id='generation-test',entry_points_arm9=list(entries),
        entry_points_arm7=[],root_map=list(roots),pages=dict(page_size=SIZE,
            dropped=0,entries=list(pages)))),encoding='utf-8')

def points(path):
    if not path.exists():return {}
    parsed=tomllib.loads(path.read_text(encoding='utf-8'))
    return {(row['addr'],row['mode']):row for row in parsed.get('entry_point',[])}

def run(work, name, inputs, images, *extra):
    out=work/name
    result=subprocess.run([sys.executable,str(TOOL),*[str(p) for p in inputs],
        '--out',str(out),'--images',str(images),'--rom-sha1',ROM,
        '--no-promote-callers',*extra],capture_output=True,text=True,encoding='utf-8')
    check(result.returncode==0,'CLI succeeds: '+name+' '+result.stderr)
    return out,json.loads((out/'ingest-report.json').read_text(encoding='utf-8'))

def main():
    with tempfile.TemporaryDirectory(prefix='nds-generation-') as temp:
        work=Path(temp);images=work/'rom';(images/'overlays').mkdir(parents=True)
        a=(0xe1a00000).to_bytes(4,'little')*(SIZE//4)
        b=(0xe3a00002).to_bytes(4,'little')*(SIZE//4)
        (images/'overlays/ovA.bin').write_bytes(a)
        (images/'overlays/ovB.bin').write_bytes(b)
        roots=[dict(cpu=9,addr=f'0x{PAGE:08X}',root_arm=bitmap([PAGE+0x900],4),root_hits=[50]*16)]
        pa=page(a,[entry(0x40),entry(0x1100),entry(1)],
                [PAGE+0x100,PAGE+0x104],root_hits=10)
        pb=page(b,[entry(0x82,mode='thumb')],
                [PAGE+0x108,PAGE+0x10c],thumb=[PAGE+0x202],root_hits=30)
        source=work/'both.json';manifest(source,[pa,pb],entries=[entry(0x40),entry(0x82,mode='thumb')],roots=roots)
        out,report=run(work,'qualified',[source],images)
        aa=points(out/'seeds/arm9_ovA_02100000.toml')
        bb=points(out/'seeds/arm9_ovB_02100000.toml')
        check((PAGE+0x40,'arm') in aa and (PAGE+0x82,'thumb') not in aa,'A receives only its own target')
        check((PAGE+0x82,'thumb') in bb and (PAGE+0x40,'arm') not in bb,'B receives only its own target and ISA')
        check((PAGE+0x100,'arm') in aa and (PAGE+0x108,'arm') in bb,'adjacent roots retain separate generation span starts')
        check((PAGE+0x104,'arm') not in aa and (PAGE+0x10c,'arm') not in bb,'one seed per generation span')
        check((PAGE+0x202,'thumb') in bb,'generation-bound Thumb root survives')
        check(all((PAGE+0x900,'arm') not in table for table in (aa,bb)),'session-only root never becomes an overlay seed')
        check(all((PAGE+0x1100,'arm') not in table and (PAGE+1,'arm') not in table for table in (aa,bb)),
              'valid page SHA does not authorize outside-page or misaligned entries')
        check(sum(report['rejected'].values())>=2,'invalid coordinates are reported')
        (work/'owned.c').write_text(f'{{0x{PAGE+0x108:08X}u, 0u, fn, &v}},\n',encoding='utf-8')
        owned,_=run(work,'owned',[source],images,'--owned-rows-arm9',str(work/'owned.c'))
        check((PAGE+0x108,'arm') in points(owned/'seeds/arm9_ovB_02100000.toml'),
              'address-only owned row cannot suppress an observed body')
        # Input ordering must not decide the module that owns an address.
        reverse=work/'reverse.json';manifest(reverse,[pb,pa],entries=[entry(0x40),entry(0x82,mode='thumb')],roots=roots)
        rev,_=run(work,'reversed',[reverse],images)
        for name in ('arm9_ovA_02100000.toml','arm9_ovB_02100000.toml'):
            check(points(out/'seeds'/name)==points(rev/'seeds'/name),'input order independent: '+name)
        # Modern empty body must not inherit a session observation from A.
        empty=work/'empty.json';manifest(empty,[pa,page(b)],entries=[entry(0x40)],roots=roots)
        clean,_=run(work,'modern-empty',[empty],images)
        check(not points(clean/'seeds/arm9_ovB_02100000.toml'),'modern empty B receives no fallback session seeds')
        # An identical page in multiple ROM modules is not unique ownership.
        (images/'overlays/ovDuplicate.bin').write_bytes(a)
        dup=work/'duplicate.json';manifest(dup,[page(a,[entry(0x40)])])
        ambiguous,amb_report=run(work,'ambiguous-rom',[dup],images)
        check(not points(ambiguous/'seeds/arm9_ovA_02100000.toml') and
              not points(ambiguous/'seeds/arm9_ovDuplicate_02100000.toml'),
              'duplicate immutable page does not choose the first module')
        configs=list((ambiguous/'images').glob('*.toml'))
        check(len(configs)==1,'ambiguous module uses one exact captured-byte bank')
        check(amb_report.get('page_versions_ambiguous_rom')==1,'ROM ambiguity is explicit in the report')
        (images/'overlays/ovDuplicate.bin').unlink()
        # Schema2 retains its old address-only usability only when unambiguous.
        legacy=work/'legacy.json';manifest(legacy,[page(a)],entries=[entry(0x40),entry(0x100,kind='root')],schema=2)
        old,_=run(work,'legacy-unique',[legacy],images)
        check((PAGE+0x40,'arm') in points(old/'seeds/arm9_ovA_02100000.toml'),'unambiguous schema2 alias remains usable')
        manifest(legacy,[page(a),page(b)],entries=[entry(0x40)],schema=2)
        old_amb,_=run(work,'legacy-ambiguous',[legacy],images)
        check(not points(old_amb/'seeds/arm9_ovA_02100000.toml') and
              not points(old_amb/'seeds/arm9_ovB_02100000.toml'),
              'schema2 cannot invent which overlay generation ran')
        manifest(legacy,[page(a)],schema=2)
        modern_session=work/'modern-session.json'
        manifest(modern_session,[],entries=[entry(0x340)])
        mixed,_=run(work,'mixed-schemas',[legacy,modern_session],images)
        check(not points(mixed/'seeds/arm9_ovA_02100000.toml'),
              'modern session-only observations cannot borrow schema2 page eligibility')
        # A target observation says nothing about its caller's ISA or body.
        caller=work/'caller.json'
        manifest(caller,[page(a,[entry(0x40,caller=f'0x{PAGE+0x300:08X}')]),page(b)],
                 entries=[entry(0x40,caller=f'0x{PAGE+0x300:08X}')])
        # Omit the helper's diagnostic suppression for this discriminating case.
        called=work/'caller-default'
        result=subprocess.run([sys.executable,str(TOOL),str(caller),'--out',str(called),
            '--images',str(images),'--rom-sha1',ROM],capture_output=True,text=True)
        check(result.returncode==0,'default caller diagnostics succeed')
        check((PAGE+0x300,'arm') in points(called/'entry_points.toml') and
              all((PAGE+0x300,'arm') not in points(p) for p in (called/'seeds').glob('*.toml')),
              'unknown caller ISA/generation remains diagnostic, never a body seed')
        # Schema3's page-bound root records and CPU identity survive unchanged.
        schema3=work/'schema3.json'
        manifest(schema3,[page(a,[entry(0x200,kind='root'),entry(0x204,kind='root')]),
                          page(b,[entry(0x88)],cpu=7)],schema=3)
        s3,_=run(work,'schema3',[schema3],images)
        check((PAGE+0x200,'arm') in points(s3/'seeds/arm9_ovA_02100000.toml') and
              (PAGE+0x204,'arm') not in points(s3/'seeds/arm9_ovA_02100000.toml') and
              (PAGE+0x88,'arm') in points(s3/'seeds/arm7_ovB_02100000.toml'),
              'schema3 roots coalesce and ARM7 remains separate')
        repeated,_=run(work,'repeated',[schema3,schema3],images,'--min-hits','10')
        check((PAGE+0x88,'arm') in points(repeated/'seeds/arm7_ovB_02100000.toml'),
              'repeated manifests merge body-bound target hits before thresholding')
        filtered,_=run(work,'filtered',[schema3],images,'--min-hits','10','--no-promote-ranges')
        check(not list((filtered/'seeds').glob('*.toml')),'target hit threshold and range opt-out are honored')
        ranked,rank_report=run(work,'ranked',[source],images,'--max-span-seeds','1')
        check(rank_report['generation_span_seeds']['arm9']==1 and
              (PAGE+0x108,'arm') in points(ranked/'seeds/arm9_ovB_02100000.toml') and
              (PAGE+0x100,'arm') not in points(ranked/'seeds/arm9_ovA_02100000.toml'),
              'span budget ranks all generations together, not once per body')
        # Invalid metadata cannot use a valid byte hash to escape its page.
        malformed=work/'malformed.json'
        bad_roots=page(a,[entry(0x40)])
        bad_roots['root_arm']=base64.b64encode(bytes(129)).decode()
        bad_hash=page(b,[entry(0x88)]);bad_hash['sha1']='0'*40
        manifest(malformed,[bad_roots,bad_hash])
        bad,bad_report=run(work,'malformed',[malformed],images)
        check(bad_report['rejected'].get('page roots: invalid bitmap')==1 and
              bad_report['rejected'].get('page: SHA-1 mismatch')==1 and
              not points(bad/'seeds/arm9_ovB_02100000.toml'),
              'oversized bitmap and wrong body hash are rejected')
        # Adjacent pages of a uniquely resolved whole module share one span.
        c=bytes((i*37+i//251)%256 for i in range(SIZE))
        d=bytes((i*19+i//127)%256 for i in range(SIZE))
        (images/'overlays/ovWide.bin').write_bytes(c+d)
        wide=work/'wide.json'
        manifest(wide,[page(c,roots=[PAGE+SIZE-4]),
                       page(d,roots=[PAGE+SIZE],base=PAGE+SIZE)])
        wide_out,_=run(work,'wide',[wide],images)
        check(set(points(wide_out/'seeds/arm9_ovWide_02100000.toml'))=={(PAGE+SIZE-4,'arm')},
              'unique whole-module identity coalesces spans across pages')
        no_images=work/'no-images';no_images.mkdir()
        captured,_=run(work,'captured-wide',[wide],no_images)
        bank=list((captured/'images').glob('*.toml'))
        check(len(bank)==1 and set(points(bank[0]))=={(PAGE+SIZE-4,'arm')} and
              bank[0].with_suffix('.bin').read_bytes()==c+d,
              'captured run keeps its exact bytes and one cross-page span')
        boundary=work/'boundary.json'
        manifest(boundary,[page(d,[dict(addr='0x00000040',kind='call',mode='arm',hits=7)],base=0),
            page(c,[dict(addr='0xFFFFF040',kind='call',mode='arm',hits=7)],base=0xfffff000)])
        edge,edge_report=run(work,'address-boundaries',[boundary],images)
        check(edge_report['page_versions_invalid_rom_placement']==2 and
              edge_report['pages_resolved_to_rom']==0 and
              len(list((edge/'images').glob('*.toml')))==2,
              'negative and overflowing whole-module placements remain captured banks')
        # Nonempty output is a deliberate error; no stale seeds or user files
        # are silently carried into a replacement corpus.
        marker=out/'owner-notes.txt';marker.write_text('preserve me',encoding='utf-8')
        before={str(p.relative_to(out)):p.read_bytes() for p in out.rglob('*') if p.is_file()}
        reused=subprocess.run([sys.executable,str(TOOL),str(empty),'--out',str(out),
            '--images',str(images)],capture_output=True,text=True)
        after={str(p.relative_to(out)):p.read_bytes() for p in out.rglob('*') if p.is_file()}
        check(reused.returncode!=0 and before==after and 'fresh or empty' in reused.stderr,
              'nonempty output fails before mutation, preserving existing and owner files')
    print('coverage generation identity: all assertions hold')
    return 0

if __name__=='__main__':raise SystemExit(main())
