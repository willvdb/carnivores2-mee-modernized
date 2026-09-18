#!/usr/bin/env python3
"""Read-only local audit replay. Outputs counts/hashes, never redistributes assets."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lodge.catalog import project
from lodge.discovery import hash_file
from lodge.profiles import inspect_set, inventory


def inspect_corpus(directory, probe=None):
    roots = sorted({p.parent.parent for p in Path(directory).rglob('*')
                    if p.is_file() and p.name.casefold() == '_res.txt' and p.parent.name.casefold() == 'huntdat'})
    report = []
    for root in roots:
        catalog = project(root)
        # Skip script overlays that do not advertise a planner.
        if not catalog['areas'] or not catalog['licenses']:
            continue
        states = inventory(root)
        before = {f['path']: hash_file(root / f['path']) for state in states for f in state['files']}
        observations = [inspect_set(root, state, probe, catalog['dialect']['effective']) for state in states]
        after = {relative: hash_file(root / relative) for relative in before}
        report.append({'edition_directory': root.name, 'source': catalog['source'],
                       'source_sha256': catalog['source_sha256'], 'dialect_observation': catalog['dialect']['observed'],
                       'counts': {key: len(catalog[key]) for key in ('areas', 'licenses', 'weapons', 'equipment')},
                       'diagnostic_codes': sorted({d['code'] for d in catalog['diagnostics']}),
                       'state_sets': [{'key': s['key'], 'files': [{'path': f['path'], 'sha256': f['sha256'],
                                                                'size': f['size'], 'layout': f['decoded']['layout']}
                                                               for f in s['files']],
                                       'diagnostic_codes': sorted({d['code'] for d in s['diagnostics']})}
                                      for s in observations],
                       'native_state_bytes_unchanged': before == after})
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--probe', type=Path)
    arguments = parser.parse_args()
    print(json.dumps(inspect_corpus(arguments.directory, arguments.probe), indent=2))
