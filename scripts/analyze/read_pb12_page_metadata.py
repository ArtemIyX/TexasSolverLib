#!/usr/bin/env python3
"""Read checkpoint page-run metadata without opening or modifying segment data."""

import argparse
import hashlib
import json
import struct
from pathlib import Path


def fnv(data: bytes) -> int:
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value or 1


def checked_bytes(path: Path, magic: bytes, schema: int) -> bytes:
    if path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError(f"manifest too large: {path}")
    data = path.read_bytes()
    if len(data) < 48 or data[:8] != magic or struct.unpack_from('<I', data, 8)[0] != schema:
        raise ValueError(f"invalid manifest header: {path}")
    if fnv(data[:-8]) != struct.unpack_from('<Q', data, len(data) - 8)[0]:
        raise ValueError(f"invalid manifest checksum: {path}")
    return data


def checkpoint(path: Path):
    data = checked_bytes(path, b'TXMWCP61', 6)
    if struct.unpack_from('<I', data, 12)[0] != 1:
        raise ValueError('unexpected checkpoint backend schema')
    fields = struct.unpack_from('<18Q', data, 16)
    cursor = 16 + 18 * 8
    components = []
    for _ in range(3):
        components.append(struct.unpack_from('<IQ', data, cursor))
        cursor += 12
    references = []
    for _ in range(3):
        count = struct.unpack_from('<Q', data, cursor)[0]
        cursor += 8
        if count > (len(data) - 8 - cursor) // 24:
            raise ValueError('truncated checkpoint segment list')
        refs = []
        for _ in range(count):
            refs.append(struct.unpack_from('<QQQ', data, cursor))
            cursor += 24
        if any(n == 0 or (i and refs[i - 1][0] >= g) for i, (g, n, _) in enumerate(refs)):
            raise ValueError('invalid checkpoint segment order')
        references.append(refs)
    return data, fields, components, references


def filter_hash(key):
    mask64 = (1 << 64) - 1
    key = ((key ^ (key >> 30)) * 0xbf58476d1ce4e5b9) & mask64
    key = ((key ^ (key >> 27)) * 0x94d049bb133111eb) & mask64
    return key ^ (key >> 31)


def inspect_store(root: Path, expected, component, page_generation, inspect_keys,
                  simulate_filter, allocated_pages):
    path = root / 'pages' / 'CURRENT'
    data = checked_bytes(path, b'TXMWEXT1', 1)
    payload = struct.unpack_from('<I', data, 12)[0]
    generation, count, batch = struct.unpack_from('<QQQ', data, 16)
    if len(data) != 48 + count * 24:
        raise ValueError(f'invalid component manifest size: {path}')
    refs = [struct.unpack_from('<QQQ', data, 40 + i * 24) for i in range(count)]
    matches = refs == expected and (payload, batch) == component and generation == page_generation
    missing = wrong_size = 0
    actual_data_bytes = actual_index_bytes = 0
    keys = set() if inspect_keys else None
    if inspect_keys and sum(item[1] for item in expected) > 100000:
        raise ValueError('key inspection exceeds the 100000-record limit')
    for gen, records, _ in expected:
        for suffix, expected_size in (('.dat', records * (payload + 8)), ('.idx', records * 8)):
            segment = path.parent / f'run_{gen}{suffix}'
            try:
                size = segment.stat().st_size
            except FileNotFoundError:
                missing += 1
                continue
            wrong_size += size != expected_size
            if suffix == '.dat':
                actual_data_bytes += size
            else:
                actual_index_bytes += size
                if keys is not None and size == expected_size:
                    with segment.open('rb') as stream:
                        raw = stream.read(expected_size + 1)
                    if len(raw) != expected_size:
                        raise ValueError(f'index length changed while reading: {segment}')
                    values = [item[0] for item in struct.iter_unpack('<Q', raw)]
                    if any(a >= b for a, b in zip(values, values[1:])):
                        raise ValueError(f'index keys are not strictly ordered: {segment}')
                    keys.update(values)
    if keys is not None and (not matches or missing or wrong_size):
        raise ValueError('cannot report distinct keys from a mismatched or incomplete store')
    simulation = None
    if simulate_filter:
        if keys is None or allocated_pages > 1000000 or (keys and (min(keys) < 1 or max(keys) > allocated_pages)):
            raise ValueError('filter simulation requires bounded keys within allocated page IDs')
        bits = bytearray(1024 * 1024)
        mask = len(bits) * 8 - 1
        for key in keys:
            h = filter_hash(key)
            for bit in (h & mask, (h >> 23) & mask):
                bits[bit // 8] |= 1 << (bit % 8)
        false_positives = 0
        for key in range(1, allocated_pages + 1):
            if key in keys:
                continue
            h = filter_hash(key)
            a, b = h & mask, (h >> 23) & mask
            if bits[a // 8] & (1 << (a % 8)) and bits[b // 8] & (1 << (b % 8)):
                false_positives += 1
        simulation = {'allocated_absent_pages': allocated_pages - len(keys),
                      'absent_filter_false_positives': false_positives,
                      'bitmap_bytes': len(bits)}
    return {'path': str(root), 'manifest_sha256': hashlib.sha256(data).hexdigest(),
            'matches_checkpoint_page_component': matches, 'missing_files': missing,
            'wrong_size_files': wrong_size, 'data_bytes': actual_data_bytes,
            'index_bytes': actual_index_bytes,
            'distinct_page_keys': len(keys) if keys is not None else None,
            'minimum_page_key': min(keys) if keys else None,
            'maximum_page_key': max(keys) if keys else None,
            'filter_simulation': simulation}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkpoint', type=Path)
    parser.add_argument('--store', type=Path, action='append', default=[])
    parser.add_argument('--inspect-keys', action='store_true',
                        help='read index keys (bounded to 100000 records per store)')
    parser.add_argument('--simulate-filter', action='store_true',
                        help='simulate the 1 MiB filter for at most 1000000 allocated page IDs')
    args = parser.parse_args()
    if args.simulate_filter and not args.inspect_keys:
        parser.error('--simulate-filter requires --inspect-keys')
    data, fields, components, refs = checkpoint(args.checkpoint)
    page_refs = refs[2]
    payload, _ = components[2]
    record_count = sum(reference[1] for reference in page_refs)
    result = {'checkpoint': str(args.checkpoint), 'checkpoint_sha256': hashlib.sha256(data).hexdigest(),
              'completed_trajectories': fields[4], 'training_generation': fields[7],
              'committed_batch': fields[8], 'page_generation': fields[14],
              'allocated_page_count': fields[15], 'page_payload_bytes': payload,
              'page_run_count': len(page_refs), 'page_record_count_including_versions': record_count,
              'expected_page_data_bytes': record_count * (payload + 8),
              'expected_page_index_bytes': record_count * 8,
              'stores': [inspect_store(root, page_refs, components[2], fields[14], args.inspect_keys,
                                       args.simulate_filter, fields[15])
                         for root in args.store],
              'limitations': ['Counts include duplicate keys across generations.',
                              'Segment checksums were not verified against data and index contents.',
                              'Matching page manifests do not verify all checkpoint components.']}
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
