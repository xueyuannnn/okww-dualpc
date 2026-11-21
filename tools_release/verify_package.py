"""Verify a prepared source or runtime ZIP against its embedded file manifest."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile


def verify(archive: Path) -> dict:
    with zipfile.ZipFile(archive) as zipped:
        names = [entry.filename for entry in zipped.infolist() if not entry.is_dir()]
        if len(names) != len({name.casefold() for name in names}):
            raise RuntimeError("Duplicate archive entries")
        roots = set()
        for name in names:
            path = PurePosixPath(name)
            if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
                raise RuntimeError("Unsafe archive path")
            roots.add(path.parts[0])
            lowered = {part.casefold() for part in path.parts}
            if lowered & {"native-full-data", "configs-dualpc", "native-teams", ".git", "__pycache__"}:
                raise RuntimeError("Profile or local state in archive")
            if "native/personal/" in name or path.name in {"dualpc.json", "native-device-settings.json"}:
                raise RuntimeError("Private configuration in archive")
            if "objects-Debug/" in name or "objects-RelWithDebInfo/" in name:
                raise RuntimeError("Qt build residue in archive")
            if path.name == "direct_url.json" or path.suffix == ".egg-link":
                raise RuntimeError("Machine-specific Python installation metadata in archive")
        if len(roots) != 1:
            raise RuntimeError("Expected one top-level package directory")
        prefix = roots.pop() + "/"
        manifest_name = next((prefix + n for n in ("PACKAGE.json", "SOURCE-MANIFEST.json", "ENVIRONMENT.json")
                              if prefix + n in names), None)
        if not manifest_name:
            raise RuntimeError("Package manifest missing")
        manifest = json.loads(zipped.read(manifest_name))
        expected = {manifest_name}
        environment = {}
        cumulative = manifest.get('package_kind') == 'environment-update'
        if cumulative:
            from prepare_environment import checked_entries, frozen
            baseline = json.loads(zipped.read(prefix + 'ENVIRONMENT.json'))
            if (baseline.get('variant') != 'environment' or baseline.get('schema') != 1 or
                    baseline.get('platform') != 'win10-11-x64' or
                    baseline.get('environment_id') != manifest.get('environment_id')):
                raise RuntimeError('Environment identity mismatch')
            environment = checked_entries(baseline)
            if any(not frozen(e['path']) for e in environment.values()):
                raise RuntimeError('Non-runtime file in environment')
        listed = set()
        for entry in manifest["files"]:
            relative = PurePosixPath(entry['path'])
            if (relative.is_absolute() or '..' in relative.parts or '\\' in entry['path'] or ':' in entry['path'] or
                    entry['path'].casefold() in listed):
                raise RuntimeError('Unsafe or duplicate manifest entry')
            listed.add(entry['path'].casefold())
            name = prefix + entry["path"].replace("\\", "/")
            baseline = environment.get(entry['path'].casefold())
            if baseline:
                if entry != baseline or name in names:
                    raise RuntimeError('Cumulative update changes a frozen runtime file')
                continue
            expected.add(name)
            with zipped.open(name) as stream:
                actual = hashlib.file_digest(stream, "sha256").hexdigest()
            if actual != entry["sha256"].lower() or zipped.getinfo(name).file_size != entry["bytes"]:
                raise RuntimeError("File hash or size mismatch: " + name)
        if set(names) != expected:
            raise RuntimeError("Manifest does not cover every archive entry")
        if cumulative and not set(environment) <= listed:
            raise RuntimeError('Environment file missing from target manifest')
        return {"archive": archive.name, "files": len(names), "ok": True, "profiles": 0,
                "cumulative": cumulative, "environment_id": manifest.get('environment_id')}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives", type=Path, nargs="+")
    args = parser.parse_args()
    print(json.dumps([verify(path) for path in args.archives], indent=2))


if __name__ == "__main__":
    main()
