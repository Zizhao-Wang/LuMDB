#!/usr/bin/env python3
"""Validate a complete YCSB trace and optionally save a canonical clean copy.

Only recognizable header tokens in data rows are removed, with an audit record.
All other invalid input is an error. The source CSV is never modified.
"""
import argparse
import collections
import csv
import json
import os
from pathlib import Path
import sys
import tempfile

ALLOWED = {
    'A': {'READ', 'UPDATE'}, 'B': {'READ', 'UPDATE'}, 'C': {'READ'},
    'D': {'READ', 'INSERT'}, 'E': {'SCAN', 'INSERT'}, 'F': {'READ', 'RMW'},
}


def prepare(source, workload, requested='auto', output=None, report=None):
    source = Path(source).resolve()
    output = Path(output).resolve() if output is not None else None
    report = Path(report).resolve() if report is not None else None
    if any(path == source for path in (output, report)):
        raise ValueError('output/report must differ from the original CSV')
    if output is not None and output == report:
        raise ValueError('output and report must differ')
    if output is not None and output.exists():
        raise ValueError(f'prepared CSV already exists: {output}')
    if requested != 'auto' and (not requested.isascii() or not requested.isdigit() or int(requested) < 1):
        raise ValueError('operations must be auto or a positive integer')
    before = source.stat()
    raw_rows = valid_rows = 0
    counts = collections.Counter()
    dropped = []
    temporary = None
    destination = None
    try:
        if output is not None:
            fd, temporary = tempfile.mkstemp(prefix='.ycsb-prepare-', dir=output.parent)
            destination = os.fdopen(fd, 'w', encoding='utf-8', newline='')
            writer = csv.writer(destination, lineterminator='\n')
            writer.writerow(['Operation', 'Key'])
        with source.open(encoding='utf-8-sig', newline='') as stream:
            reader = csv.reader(stream, strict=True)
            header = next(reader, None)
            if header is None or [x.strip() for x in header] != ['Operation', 'Key']:
                raise ValueError(f'{source}: expected Operation,Key header')
            for row in reader:
                line = reader.line_num
                raw_rows += 1
                if len(row) != 2:
                    raise ValueError(f'{source}:{line}: expected two fields, got {row!r}')
                operation, key = (x.strip() for x in row)
                if (operation == 'Operation' and key == 'Key') or (operation in ALLOWED[workload] and key == 'Key'):
                    dropped.append({'line': line, 'operation': operation, 'key': key,
                                    'reason': 'header_token_in_data'})
                    continue
                if operation not in ALLOWED[workload]:
                    raise ValueError(f'{source}:{line}: invalid operation for YCSB-{workload}: {operation!r}')
                if not key or not key.isascii() or not key.isdigit() or int(key) > (1 << 64) - 1:
                    raise ValueError(f'{source}:{line}: invalid uint64 key: {key!r}')
                valid_rows += 1
                counts[operation] += 1
                if output is not None:
                    writer.writerow([operation, key])
        after = source.stat()
        if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
            raise ValueError(f'CSV changed during validation: {source}')
        operations = valid_rows if requested == 'auto' else int(requested)
        if operations < 1 or operations > valid_rows:
            raise ValueError(f'requested {operations} operations, but only {valid_rows} valid records in {source}')
        result = {'source': str(source), 'workload': workload, 'raw_records': raw_rows,
                  'valid_records': valid_rows, 'dropped_records': len(dropped),
                  'effective_operations': operations, 'requested_operations': requested,
                  'valid_operation_counts': dict(counts), 'dropped_rows': dropped,
                  'prepared_csv': str(output) if output is not None else None}
        if destination is not None:
            destination.close()
            destination = None
            os.replace(temporary, output)
            temporary = None
        if report is not None:
            report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        return result
    finally:
        if destination is not None:
            destination.close()
        if temporary is not None:
            Path(temporary).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True)
    parser.add_argument('--workload', required=True, choices=ALLOWED)
    parser.add_argument('--operations', default='auto')
    parser.add_argument('--output')
    parser.add_argument('--report')
    parser.add_argument('--tsv', action='store_true', help='emit numeric shell-readable summary')
    args = parser.parse_args()
    try:
        result = prepare(args.input, args.workload, args.operations, args.output, args.report)
    except (OSError, ValueError, csv.Error) as error:
        print(f'YCSB validation failed: {error}', file=sys.stderr)
        return 1
    if args.tsv:
        print(result['effective_operations'], result['raw_records'], result['valid_records'], result['dropped_records'], sep='\t')
    else:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
