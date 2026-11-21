"""Build one immutable environment and cumulative application updates.

Application archives contain the complete current application, rather than a
chain of changes from previous releases. Runtime changes require a new baseline.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import zipfile

from prepare_binary import APPLICATIONS, COPY_IGNORE, ENGINE_FILES, ENGINE_FOLDERS, PLATFORM, RUNTIME_FOLDERS, windows_path
from prepare_source import ROOT, digest

PACKAGE_ROOT = 'OKWW-DualPC-full'
UPDATE_NAME = f'OK-WW-DualPC-update-{PLATFORM}.zip'


def frozen(name: str) -> bool:
    parts = PurePosixPath(name).parts
    return (len(parts) == 1 and parts[0].lower().endswith('.dll') or
            parts[0] in RUNTIME_FOLDERS or
            name.startswith(('workshop-engine/runtime/', 'assets/ocr/', 'assets/echo_model/',
                             'workshop-engine/assets/ocr/', 'workshop-engine/assets/echo_model/')))


def checked_entries(manifest: dict) -> dict[str, dict]:
    result = {}
    for entry in manifest['files']:
        name = entry['path']
        path = PurePosixPath(name)
        if (path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name or
                name.casefold() in result or path.parts[0] in {'native-full-data', 'configs-dualpc', 'native-teams'}):
            raise RuntimeError('Unsafe or duplicate managed path: ' + name)
        result[name.casefold()] = entry
    return result


def environment_manifest(base: dict) -> dict:
    if base.get('personal_profiles', 0) or base.get('variant') != 'full':
        raise RuntimeError('An ordinary package is required as the environment baseline')
    if base.get('platform', PLATFORM) != PLATFORM:
        raise RuntimeError('Environment platform mismatch')
    files = sorted((entry for entry in checked_entries(base).values() if frozen(entry['path'])), key=lambda e: e['path'])
    if not any(e['path'] == 'workshop-engine/runtime/python.exe' for e in files):
        raise RuntimeError('Portable Python is missing from the environment')
    fingerprint = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    return dict(schema=1, variant='environment', platform=PLATFORM,
                environment_id=PLATFORM + '-' + fingerprint[:12], fingerprint=fingerprint,
                personal_profiles=0, files=files)


def make_environment(base: Path, review: Path) -> tuple[dict, Path]:
    manifest = environment_manifest(json.loads((base / 'PACKAGE.json').read_text('utf-8')))
    review.mkdir(parents=True, exist_ok=True)
    archive = review / ('OK-WW-DualPC-environment-' + manifest['environment_id'] + '.zip')
    encoded = json.dumps(manifest, indent=2).encode()
    if archive.exists():
        raise RuntimeError('Environment archive already exists; do not overwrite a published baseline')
    temporary = archive.with_suffix('.zip.tmp')
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=5) as zipped:
        zipped.writestr(PACKAGE_ROOT + '/ENVIRONMENT.json', encoded)
        for entry in manifest['files']:
            file = windows_path(base / entry['path'])
            if digest(file) != entry['sha256'] or file.stat().st_size != entry['bytes']:
                raise RuntimeError('Baseline file differs from its manifest: ' + entry['path'])
            zipped.write(file, PACKAGE_ROOT + '/' + entry['path'])
    temporary.replace(archive)
    (review / 'ENVIRONMENT.json').write_bytes(encoded)
    return manifest, archive


def write_cumulative(stage: Path, environment: dict, version: str, review: Path) -> Path:
    if environment.get('variant') != 'environment' or environment.get('platform') != PLATFORM:
        raise RuntimeError('Select an environment for this platform')
    environment_files = checked_entries(environment)
    payload = []
    for file in sorted(windows_path(stage).rglob('*')):
        if not file.is_file():
            continue
        name = file.relative_to(windows_path(stage)).as_posix()
        if name in {'PACKAGE.json', '.cumulative-stage.json'}:
            continue
        if name.casefold() in environment_files or frozen(name):
            raise RuntimeError('Runtime changes require a new environment: ' + name)
        payload.append(dict(path=name, bytes=file.stat().st_size, sha256=digest(file)))
    if not any(e['path'] == 'OKWW-DualPC-Native-Dev.exe' for e in payload):
        raise RuntimeError('Application executable is missing')
    files = sorted([*environment['files'], *payload], key=lambda e: e['path'])
    checked_entries({'files': files})
    manifest = dict(version='0.2.0', channel='preview', variant='full', edition='standard',
                    release_version=version, platform=PLATFORM, package_kind='environment-update',
                    environment_id=environment['environment_id'], runtime='C++ + Python workshop',
                    core_language='Rust', frontend='Qt6 Widgets', personal_profiles=0,
                    exe_sha256=digest(stage / 'OKWW-DualPC-Native-Dev.exe'), files=files)
    (stage / 'PACKAGE.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    archive = review / UPDATE_NAME
    temporary = archive.with_suffix('.zip.tmp')
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=5) as zipped:
        zipped.write(stage / 'PACKAGE.json', PACKAGE_ROOT + '/PACKAGE.json')
        for entry in payload:
            zipped.write(windows_path(stage / entry['path']), PACKAGE_ROOT + '/' + entry['path'])
    temporary.replace(archive)
    return archive


def assemble(base: Path, build: Path, review: Path, environment: dict, refresh: bool = False) -> Path:
    stage = review / 'OKWW-DualPC-update'
    if stage.exists():
        marker = stage / '.cumulative-stage.json'
        if (not refresh or stage.is_symlink() or ROOT not in stage.resolve().parents or
                stage.resolve().parent != review.resolve() or not marker.is_file() or
                json.loads(marker.read_text()).get('owner') != 'OKWW-DualPC-update' or
                any((stage / name).exists() for name in ('native-full-data', 'native-teams', '.native-session.lock'))):
            raise RuntimeError('Use a fresh application stage, or refresh this tool-owned stage')
        shutil.rmtree(windows_path(stage))
    stage.mkdir(parents=True)
    (stage / '.cumulative-stage.json').write_text('{"owner":"OKWW-DualPC-update"}')
    base_manifest = json.loads((base / 'PACKAGE.json').read_text('utf-8'))
    for entry in checked_entries(base_manifest).values():
        name = entry['path']
        if frozen(name) or name.startswith('source/') or name == '.public-package.json':
            continue
        source, destination = windows_path(base / name), windows_path(stage / name)
        if digest(source) != entry['sha256']:
            raise RuntimeError('Base application file changed: ' + name)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    # Replace owned code trees as a complete snapshot, including removals.
    code_trees = ['assets', 'icons', 'native/resources', 'workshop-engine/workshop/bridge',
                  *('workshop-engine/' + name for name in ENGINE_FOLDERS)]
    for name in code_trees:
        target = stage / name
        if target.exists():
            assert stage.resolve() in target.resolve().parents and not target.is_symlink()
            shutil.rmtree(windows_path(target))
        source = ROOT / name.removeprefix('workshop-engine/')
        def ignore(directory, names):
            ignored = set(COPY_IGNORE(directory, names))
            for child in names:
                relative = name + '/' + (Path(directory) / child).relative_to(source).as_posix()
                if frozen(relative) or frozen(relative + '/'):
                    ignored.add(child)
            return ignored
        shutil.copytree(source, target, ignore=ignore)
    for name in APPLICATIONS:
        shutil.copy2(build / name, stage / name)
    for name in ENGINE_FILES:
        shutil.copy2(ROOT / name, stage / 'workshop-engine' / name)
    for entry in environment['files']:
        candidate = ROOT / entry['path'].removeprefix('workshop-engine/')
        if candidate.is_file() and digest(candidate) != entry['sha256']:
            raise RuntimeError('Rebuild environment for changed dependency: ' + entry['path'])
    (stage / 'ENVIRONMENT.json').write_text(json.dumps(environment, indent=2), encoding='utf-8')
    shutil.copy2(ROOT / 'docs/release-notes.md', stage / 'ReleaseNotes.md')
    (stage / 'README.md').write_text(
        '# OK-WW双机版\n\n首次安装：先解压对应环境包，再把本版本增量包解压到同一目录。'
        '已有软件：可在“关于”中检查更新，直接更新到目标版本，无需逐版覆盖。\n\n'
        '启动 OKWW-DualPC-Native-Dev.exe。个人配置保存在 native-full-data，更新前请备份。\n\n'
        f'环境标识：{environment["environment_id"]}。Windows 10／11 x64。\n\n'
        '[项目与下载](https://github.com/xueyuannnn/okww-dualpc) · '
        '[使用说明](https://github.com/xueyuannnn/okww-dualpc/blob/main/docs/usage.md)\n', encoding='utf-8')
    return stage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-package', type=Path, required=True)
    parser.add_argument('--native-build', type=Path, required=True)
    parser.add_argument('--review', type=Path, required=True)
    parser.add_argument('--environment', type=Path, help='Reuse this fixed ENVIRONMENT.json')
    parser.add_argument('--refresh-stage', action='store_true', help='Replace only the tool-owned application stage')
    args = parser.parse_args()
    base, build, review = args.base_package.resolve(), args.native_build.resolve(), args.review.resolve()
    if args.environment:
        environment = json.loads(args.environment.read_text('utf-8'))
        existing = review / ('OK-WW-DualPC-environment-' + environment['environment_id'] + '.zip')
        archive = existing if existing.is_file() else None
    else:
        environment, archive = make_environment(base, review)
    stage = assemble(base, build, review, environment, args.refresh_stage)
    update = write_cumulative(stage, environment, (ROOT / 'native/RELEASE_VERSION').read_text().strip(), review)
    assets = [update, *([archive] if archive else [])]
    (review / 'SHA256SUMS.txt').write_text(''.join(digest(p) + '  ' + p.name + '\n' for p in assets), encoding='utf-8')
    print(json.dumps(dict(environment_id=environment['environment_id'],
                         archives=[dict(path=str(p), bytes=p.stat().st_size) for p in assets])), flush=True)


if __name__ == '__main__':
    main()
