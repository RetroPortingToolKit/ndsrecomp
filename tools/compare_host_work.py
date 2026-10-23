"""Compare pinned run records without ignoring guest-state differences."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct


def normalized_gp3d(record_path):
    """Only discard proven nonsemantic CmdFIFOEntry padding, not FIFO fields.

    Current serializer: vector length, 16-byte MELN header, 16-byte GP3D header,
    12-byte FIFO metadata, 256 x (u32 Param,u8 Command,3 unused union bytes),
    then 12-byte PIPE metadata and four entries. Do not normalize outside these
    first two FIFOs or silently tolerate a changed serializer layout.
    """
    data = (record_path.parent / 'after.state').read_bytes()
    assert data[:8] == b'NDSSTATE' and struct.unpack_from('<I', data, 8)[0] == 4
    count = struct.unpack_from('<I', data, 12)[0]
    for i in range(count):
        tag, revision, offset, size, crc, flags = struct.unpack_from('<IIQQII', data, 24 + i * 32)
        if tag.to_bytes(4, 'little') == b'GP3D':
            payload = bytearray(data[offset:offset + size])
            assert payload[4:8] == b'MELN' and payload[20:24] == b'GP3D'
            assert struct.unpack_from('<I', payload)[0] + 12 == size
            for begin, entries in ((48, 256), (2108, 4)):
                for entry in range(entries):
                    start = begin + entry * 8 + 5
                    payload[start:start + 3] = b'\0\0\0'
            return payload
    raise RuntimeError('missing GP3D')


def compare(reference, candidate, reference_path=None, candidate_path=None):
    # IDEN legitimately distinguishes different artifacts; no guest section is
    # exempt. The loader identity checks remain enforced during every run.
    sections = [name for name in reference['sections'] if name != 'IDEN']
    different = [name for name in sections if reference['sections'][name] != candidate['sections'][name]]
    padding_only = False
    if different == ['GP3D'] and reference_path and candidate_path:
        padding_only = normalized_gp3d(reference_path) == normalized_gp3d(candidate_path)
    events = {}
    for name in reference['after']['events']:
        a = reference['after']['events'][name] - reference['before']['events'][name]
        b = candidate['after']['events'][name] - candidate['before']['events'][name]
        if a != b:
            events[name] = [a, b]
    return dict(different_sections=different,
        gp3d_difference_only_unused_fifo_padding=padding_only,
        same_pixels=reference['pixels'] == candidate['pixels'],
        same_audio_tail=(reference['audio']['count'] == candidate['audio']['count'] and
                         reference['audio']['sha256'] == candidate['audio']['sha256']),
        event_delta_differences=events,
        different_before_scene=reference['before']['scene'] != candidate['before']['scene'],
        different_after_scene=reference['after']['scene'] != candidate['after']['scene'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('reference', type=Path)
    p.add_argument('candidates', type=Path, nargs='+')
    p.add_argument('--out', type=Path)
    args = p.parse_args()
    reference = json.loads(args.reference.read_text())
    result = {str(path): compare(reference, json.loads(path.read_text()), args.reference, path) for path in args.candidates}
    text = json.dumps(result, indent=2) + '\n'
    if args.out:
        if args.out.resolve().drive.upper() == 'C:':
            p.error('use non-C: output')
        args.out.write_text(text)
    print(text)
    # Event counts are host history, but their deltas are useful diagnostics;
    # unlike serialized state/pixels/audio, a difference is not silently passed.
    if any((v['different_sections'] and not v['gp3d_difference_only_unused_fifo_padding']) or
           not v['same_pixels'] or not v['same_audio_tail'] for v in result.values()):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
