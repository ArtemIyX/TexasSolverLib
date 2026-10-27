#!/usr/bin/env python3
"""Copy the recorded External store into a new isolated directory, fail closed."""

import argparse
import hashlib
import json
from pathlib import Path

from audit_pb12_snapshot import digest, inventory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('destination', type=Path, help='new, nonexistent snapshot parent directory')
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding='utf-8-sig'))
    if manifest['schema_version'] != 1:
        raise ValueError('unsupported snapshot manifest')
    source = Path(manifest['source_directory']).resolve(strict=True)
    parent = args.destination.parent.resolve(strict=True)
    destination = parent / args.destination.name
    if destination.exists() or destination == source or source in destination.parents or destination in source.parents:
        raise ValueError('snapshot destination exists or overlaps source')
    records = {}
    for record in manifest['files']:
        relative = Path(record['path'])
        if relative.is_absolute() or '..' in relative.parts or str(relative) in records:
            raise ValueError('unsafe or duplicate manifest path')
        records[str(relative)] = (record['length'], record['sha256'].upper())
    if len(records) != manifest['file_count'] or sum(x[0] for x in records.values()) != manifest['byte_count']:
        raise ValueError('manifest totals disagree')
    if inventory(source) != set(records):
        raise ValueError('source inventory changed')
    markers = {'TRAINING_BATCH.wal', 'TRAINING_BATCH.tmp', 'CHECKPOINT_RESTORE.wal',
               'CHECKPOINT_RESTORE.wal.tmp', 'TRAINING_CURRENT.tmp', 'CHECKPOINT_TRAINING_ROLLBACK.tmp',
               'CATALOG_CURRENT.previous', 'ROWS_CURRENT.previous', 'PAGES_CURRENT.previous',
               'CHECKPOINT_CATALOG.previous', 'CHECKPOINT_ROWS.previous', 'CHECKPOINT_PAGES.previous',
               'CHECKPOINT_TRAINING.previous', 'BATCH.wal', 'CURRENT.tmp', 'CURRENT.restore.tmp',
               'CHECKPOINT_CURRENT.tmp', 'CHECKPOINT_ROLLBACK.tmp'}
    if any(Path(name).name in markers or Path(name).name.endswith('.PIN.tmp') for name in records):
        raise ValueError('source contains unfinished transaction or pin markers')
    destination.mkdir()
    store = destination / 'store'
    store.mkdir()
    for name, expected in records.items():
        src, dst = source / name, store / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        before = src.stat()
        hasher = hashlib.sha256()
        with src.open('rb') as reader, dst.open('xb') as writer:
            for block in iter(lambda: reader.read(1024 * 1024), b''):
                hasher.update(block)
                writer.write(block)
        after = src.stat()
        if ((before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns) or
                (before.st_size, hasher.hexdigest().upper()) != expected):
            raise ValueError(f'source changed or hash mismatch while copying: {name}')
    manifest['snapshot_store_directory'] = str(store)
    output = destination / 'snapshot_manifest.json'
    with output.open('x', encoding='utf-8') as stream:
        json.dump(manifest, stream, indent=2)
    print(json.dumps({'snapshot': str(store), 'manifest': str(output), 'files': len(records),
                      'bytes': manifest['byte_count'], 'copy_complete': True}, indent=2))


if __name__ == '__main__':
    main()
