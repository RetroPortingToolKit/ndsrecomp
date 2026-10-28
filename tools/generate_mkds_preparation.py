#!/usr/bin/env python3
"""Generate AMCE0 native-preparation hints from a private natural-load census.

Inputs are the arm9_native_compilations JSON export and matching full Main-RAM
and ITCM snapshots. Output contains only locations and SHA-1 page fingerprints,
never guest instruction bytes. This does not qualify new game/code revisions.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path


def generate(locations, mainram, itcm):
    if len(mainram) != 4 * 1024 * 1024 or len(itcm) != 32 * 1024:
        raise ValueError('expected matching 4 MiB Main-RAM and 32 KiB ITCM snapshots')
    pages = defaultdict(set)
    for row in locations['arm9']:
        pc, thumb, descriptor = row['pc'], row['thumb'], int(row['descriptor'])
        if type(pc) is not int or type(thumb) is not bool or descriptor != pc | (int(thumb) << 32):
            raise ValueError(f'unsupported location descriptor: {row}')
        if pc & (1 if thumb else 3):
            raise ValueError(f'unaligned location: {row}')
        # Observed resident AMCE0 main-code/ITCM ranges only. Overlay expansion
        # needs its own image identity/lifecycle, not a broader address guess.
        if not (0x01ff8000 <= pc < 0x02000000 or 0x02000000 <= pc < 0x021773b8):
            raise ValueError(f'location outside qualified resident ranges: {pc:#x}')
        pages[pc & ~4095].add(descriptor)
    if not pages:
        raise ValueError('no ARM9 locations supplied')
    lines = [
        '// AMCE0 / SHA1 691e00d9a5dd80b04f80cc7559503e8b06848785.',
        '// Profile: natural menu -> Waluigi Pinball 150cc construction, 180 VBlanks.',
        '// Hints cover observed common race setup in main code/ITCM; not all-course coverage.',
        '// PCs and SHA1 page fingerprints only. Existing live-code guards remain authoritative.',
        'constexpr uint64_t preparation_locations[] = {',
    ]
    index, entries = 0, []
    for page, descriptors in sorted(pages.items()):
        descriptors = sorted(descriptors)
        for at in range(0, len(descriptors), 8):
            lines.append('    ' + ','.join(f'0x{value:016x}ull' for value in descriptors[at:at + 8]) + ',')
        data, offset = (itcm, page & 32767) if page < 0x02000000 else (mainram, page - 0x02000000)
        digest = hashlib.sha1(data[offset:offset + 4096]).hexdigest()
        entries.append((page, index, index + len(descriptors), digest))
        index += len(descriptors)
    lines.extend(['};', 'constexpr PreparationPage preparation_pages[] = {'])
    for page, first, end, digest in entries:
        lines.append(f'    {{0x{page:08x}u,{first},{end},"{digest}"}},')
    lines.extend(['};', ''])
    return '\n'.join(lines), len(pages), index


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--locations', type=Path, required=True)
    parser.add_argument('--mainram', type=Path, required=True)
    parser.add_argument('--itcm', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    text, pages, locations = generate(json.loads(args.locations.read_text(encoding='utf-8-sig')),
                                     args.mainram.read_bytes(), args.itcm.read_bytes())
    args.out.write_text(text, encoding='utf-8', newline='\n')
    print(json.dumps(dict(pages=pages, locations=locations,
                          sha256=hashlib.sha256(args.out.read_bytes()).hexdigest(), guest_bytes=False)))


if __name__ == '__main__':
    main()
