#!/usr/bin/env python3
"""Audit an existing snapshot against its recorded manifest without modifying it."""

import argparse
import hashlib
import json
import os
from pathlib import Path


def digest(path: Path):
    before = path.stat()
    hasher = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            hasher.update(block)
    after = path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError(f'file changed while hashing: {path}')
    return before.st_size, hasher.hexdigest().upper()


def inventory(root: Path):
    files = set()
    for directory, names, filenames in os.walk(root, followlinks=False):
        for name in names:
            if (Path(directory) / name).is_symlink():
                raise ValueError(f'symlink directory: {name}')
        for name in filenames:
            path = Path(directory) / name
            if path.is_symlink():
                raise ValueError(f'symlink file: {path}')
            files.add(str(path.relative_to(root)))
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--source-only', action='store_true', help='audit source only after a run')
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding='utf-8-sig'))
    if manifest['schema_version'] != 1:
        raise ValueError('unsupported snapshot manifest')
    source = Path(manifest['source_directory']).resolve(strict=True)
    snapshot = Path(manifest['snapshot_store_directory']).resolve(strict=True)
    if source == snapshot or source in snapshot.parents or snapshot in source.parents:
        raise ValueError('source and snapshot overlap')
    expected = {}
    for record in manifest['files']:
        relative = Path(record['path'])
        if relative.is_absolute() or '..' in relative.parts or str(relative) in expected:
            raise ValueError('unsafe or duplicate manifest path')
        expected[str(relative)] = (record['length'], record['sha256'].upper())
    if len(expected) != manifest['file_count'] or sum(x[0] for x in expected.values()) != manifest['byte_count']:
        raise ValueError('snapshot manifest totals disagree')
    result = {'source': str(source), 'snapshot': str(snapshot), 'expected_files': len(expected),
              'expected_bytes': manifest['byte_count'], 'stores': {}}
    stores = [('source', source)] if args.source_only else [('source', source), ('snapshot', snapshot)]
    for label, root in stores:
        actual = inventory(root)
        expected_names = set(expected)
        missing, extra = sorted(expected_names - actual), sorted(actual - expected_names)
        mismatches = []
        for name in sorted(expected_names & actual):
            if digest(root / name) != expected[name]:
                mismatches.append(name)
        result['stores'][label] = {'actual_files': len(actual), 'missing_count': len(missing),
                                   'extra_count': len(extra), 'mismatch_count': len(mismatches),
                                   'missing_examples': missing[:10], 'extra_examples': extra[:10],
                                   'mismatch_examples': mismatches[:10]}
    result['passed'] = all(not (item['missing_count'] or item['extra_count'] or item['mismatch_count'])
                           for item in result['stores'].values())
    print(json.dumps(result, indent=2))
    if not result['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
