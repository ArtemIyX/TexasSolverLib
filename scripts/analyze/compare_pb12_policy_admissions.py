#!/usr/bin/env python3
"""Compare replay policy keys with External training admissions and policy rows."""

import argparse
import hashlib
import json
import mmap
import struct
from pathlib import Path


U64 = struct.Struct('<Q')
U32 = struct.Struct('<I')
ROW_MAGIC = 0x3152574D
CATALOG_MENU_MAGIC = 0x314D574D


def fnv1a(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value or 1


def read_current(directory, expected_payload_bytes=None):
    data = (directory / 'CURRENT').read_bytes()
    if data[:8] != b'TXMWEXT1' or len(data) < 48:
        raise ValueError(f'invalid External CURRENT manifest: {directory}')
    schema, payload_bytes = struct.unpack_from('<II', data, 8)
    generation, run_count, last_batch = struct.unpack_from('<QQQ', data, 16)
    if (schema != 1 or len(data) != 48 + run_count * 24 or
            fnv1a(data[:-8]) != U64.unpack_from(data, len(data) - 8)[0]):
        raise ValueError(f'invalid External CURRENT manifest checksum/schema: {directory}')
    if expected_payload_bytes is not None and payload_bytes != expected_payload_bytes:
        raise ValueError(f'unexpected payload width in {directory}: {payload_bytes}')
    runs = [struct.unpack_from('<QQQ', data, 40 + i * 24) for i in range(run_count)]
    return payload_bytes, runs, generation, last_batch


def lookup_record(directory, key, payload_bytes):
    _width, runs, _generation, _last_batch = read_current(directory, payload_bytes)
    record_width = 8 + payload_bytes
    for generation, count, _content_hash in reversed(runs):
        if count == 0:
            continue
        index_path = directory / f'run_{generation}.idx'
        data_path = directory / f'run_{generation}.dat'
        if (index_path.stat().st_size != count * 8 or
                data_path.stat().st_size != count * record_width):
            raise ValueError(f'External run file size mismatch: {generation}')
        with index_path.open('rb') as index:
            first = U64.unpack(index.read(8))[0]
            index.seek((count - 1) * 8)
            last = U64.unpack(index.read(8))[0]
            if count > 1 and first >= last:
                raise ValueError(f'External index bounds are invalid: {index_path}')
            if key < first or key > last:
                continue
            low, high = 0, count
            while low < high:
                middle = low + (high - low) // 2
                index.seek(middle * 8)
                candidate = U64.unpack(index.read(8))[0]
                if candidate < key:
                    low = middle + 1
                else:
                    high = middle
            if low == count:
                continue
            index.seek(low * 8)
            if U64.unpack(index.read(8))[0] != key:
                continue
        with data_path.open('rb') as records:
            records.seek(low * record_width)
            stored_key = U64.unpack(records.read(8))[0]
            payload = records.read(payload_bytes)
        if stored_key != key or len(payload) != payload_bytes:
            raise ValueError(f'External index/data key mismatch: {generation}')
        return payload, generation
    return None


def catalog_menu(payload, public_state_id):
    node_bytes = 225
    if len(payload) < node_bytes + 20 or U64.unpack_from(payload, 16)[0] != public_state_id:
        raise ValueError('public catalog state record is invalid')
    menu = payload[node_bytes:]
    magic, schema, count = struct.unpack_from('<III', menu, 0)
    max_actions, remainder = divmod(len(menu) - 20, 17)
    if magic != CATALOG_MENU_MAGIC or schema != 1 or remainder or count > max_actions:
        raise ValueError('public catalog menu header/width is invalid')
    if fnv1a(menu[:-8]) != U64.unpack_from(menu, len(menu) - 8)[0]:
        raise ValueError('public catalog menu checksum is invalid')
    if count == 0:
        return None, 0
    menu_ids = []
    for index in range(count):
        _kind, action_index, target, menu_id = struct.unpack_from('<BIiQ', menu, 12 + index * 17)
        if action_index != index or target < 0 or menu_id == 0:
            raise ValueError('public catalog action descriptor is invalid')
        menu_ids.append(menu_id)
    if len(set(menu_ids)) != 1:
        raise ValueError('public catalog actions have inconsistent menu IDs')
    return menu_ids[0], count


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest().upper()


def compare(args):
    replay = json.loads(args.diagnostics.read_text(encoding='utf-8-sig'))
    export_report = json.loads((args.policy.parent / 'report.json').read_text(encoding='utf-8-sig'))
    samples = replay['lookup_diagnostics']['samples']
    keys = {}
    for sample in samples:
        item = sample['key']
        key = (int(item['public_state_id']), int(item['seat']), int(item['bucket']), int(item['action_menu_id']))
        keys[key] = int(sample['replay_lookup_ordinal'])
    if not keys or len(keys) != len(samples):
        raise ValueError('replay diagnostics contain no keys or duplicate keys')

    rows_dir, catalog_dir = args.store / 'rows', args.store / 'catalog'
    row_width, row_runs, row_generation, row_batch = read_current(rows_dir, 152)
    catalog_width, catalog_runs, catalog_generation, catalog_batch = read_current(catalog_dir)
    if catalog_width < 245 or (catalog_width - 245) % 17:
        raise ValueError(f'unexpected catalog payload width: {catalog_width}')

    training = {}
    for public_state_id, seat, bucket, requested_menu in keys:
        row_result = lookup_record(rows_dir, public_state_id, row_width)
        catalog_result = lookup_record(catalog_dir, public_state_id, catalog_width)
        actual_menu, menu_actions = (None, 0)
        if catalog_result is not None:
            actual_menu, menu_actions = catalog_menu(catalog_result[0], public_state_id)

        shape = None
        if row_result is not None:
            payload = row_result[0]
            if U32.unpack_from(payload, 0)[0] != ROW_MAGIC or U32.unpack_from(payload, 4)[0] != 1:
                raise ValueError('row index metadata header is invalid')
            slot = 8 + seat * 24
            present, action_count = payload[slot], payload[slot + 1]
            if present not in (0, 1):
                raise ValueError('row index seat presence flag is invalid')
            if present:
                shape = {
                    'action_count': action_count,
                    'bucket_count': U32.unpack_from(payload, slot + 4)[0],
                    'generation': row_result[1],
                }

        bucket_present = bool(shape and bucket < shape['bucket_count'])
        exact_admission = bool(shape and bucket_present and shape['action_count'] == menu_actions
                               and actual_menu == requested_menu)
        training[(public_state_id, seat, bucket, requested_menu)] = {
            'row_record_present': row_result is not None,
            'row_shape': shape,
            'catalog_state_present': catalog_result is not None,
            'catalog_menu_id': actual_menu,
            'catalog_action_count': menu_actions,
            'bucket_in_row_shape': bucket_present,
            'menu_matches': actual_menu == requested_menu,
            'exact_training_key_admitted': exact_admission,
        }

    exported = {}
    with args.policy.open('rb') as stream:
        mapped = mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ)
        if mapped[:8] != b'MWFB0002' or U32.unpack_from(mapped, 8)[0] != 2:
            raise ValueError('exported policy is not a supported full-blueprint stream')
        row_count = U64.unpack_from(mapped, 168)[0]
        if row_count != export_report.get('export_row_count'):
            raise ValueError('exported policy row count disagrees with export report')
        cursor, end = 176, len(mapped) - 8
        previous = None
        for row_number in range(row_count):
            if cursor + 28 > end:
                raise ValueError(f'policy row {row_number} header is truncated')
            state, seat, bucket, menu, action_count = struct.unpack_from('<QiIQI', mapped, cursor)
            key = (state, seat, bucket, menu)
            if action_count == 0 or action_count > 8 or (previous is not None and key <= previous):
                raise ValueError(f'policy row {row_number} has invalid shape or order')
            cursor += 28
            actions_end = cursor + action_count * 19
            if actions_end > end:
                raise ValueError(f'policy row {row_number} actions are truncated')
            if key in keys:
                for index in range(action_count):
                    action_menu = U64.unpack_from(mapped, cursor + index * 19 + 9)[0]
                    if action_menu != menu:
                        raise ValueError('exported policy action menu disagrees with its row key')
                exported[key] = {'row_number': row_number, 'action_count': action_count}
            cursor = actions_end
            previous = key
        trailer_hash = U64.unpack_from(mapped, end)[0]
        mapped.close()
    if cursor != end or trailer_hash != export_report.get('export_result_payload_hash'):
        raise ValueError('exported policy stream size or trailer hash disagrees with report')

    preserved_hash, attempt_hash = sha256(args.preserved_policy), sha256(args.policy)
    if preserved_hash != attempt_hash:
        raise ValueError('attempt policy is not byte-identical to preserved policy')

    results = []
    for key, ordinal in sorted(keys.items(), key=lambda item: item[1]):
        admission = training[key]
        export_row = exported.get(key)
        if admission['exact_training_key_admitted'] and export_row is None:
            classification = 'training_admitted_export_missing'
        elif not admission['exact_training_key_admitted'] and export_row is None:
            classification = 'training_coverage_gap'
        elif admission['exact_training_key_admitted']:
            classification = 'training_and_export_present'
        else:
            classification = 'export_present_training_identity_mismatch'
        results.append({
            'lookup_ordinal': ordinal,
            'public_state_id': key[0], 'seat': key[1], 'bucket': key[2], 'action_menu_id': key[3],
            **admission, 'export_row': export_row, 'classification': classification,
        })

    return {
        'store': str(args.store),
        'row_manifest': {'payload_bytes': row_width, 'run_count': len(row_runs), 'generation': row_generation, 'last_batch': row_batch},
        'catalog_manifest': {'payload_bytes': catalog_width, 'run_count': len(catalog_runs), 'generation': catalog_generation, 'last_batch': catalog_batch},
        'policy': {'row_count': row_count, 'trailer_payload_hash': trailer_hash,
                   'report_payload_hash': export_report.get('export_result_payload_hash'),
                   'preserved_sha256': preserved_hash, 'attempt_sha256': attempt_hash},
        'results': results,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('diagnostics', type=Path)
    parser.add_argument('policy', type=Path, help='attempt policy.mwfb; report.json is read beside it')
    parser.add_argument('preserved_policy', type=Path)
    parser.add_argument('store', type=Path, help='External store root; read-only binary inspection')
    args = parser.parse_args()
    print(json.dumps(compare(args), indent=2))


if __name__ == '__main__':
    main()
