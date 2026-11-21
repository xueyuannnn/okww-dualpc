"""Report Win7 porting work and guard optional Windows API calls.

This source audit is not a substitute for testing a complete package on Win7.
Run with the development Python environment, not the legacy interpreter.
"""
from __future__ import annotations

import argparse
import ast
import json
from pathlib import Path
import re
import tomllib

ROOT = Path(__file__).resolve().parents[1]
OPTIONAL_APIS = {
    'CreateDXGIFactory2', 'SetThreadDescription', 'GetThreadDescription',
    'GetDpiForWindow', 'GetDpiForSystem', 'SetThreadDpiAwarenessContext',
    'SetProcessDpiAwarenessContext', 'GetSystemTimePreciseAsFileTime',
    'WaitOnAddress', 'WakeByAddressAll', 'WakeByAddressSingle', 'CreateFile2',
    'GetOverlappedResultEx', 'IsWow64Process2',
}
# Keep newlines while dropping comments and quoted API names passed to
# GetProcAddress. A dynamic lookup must not look like a static function call.
CPP_IGNORED = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
API_CALL = re.compile(r'\b(' + '|'.join(sorted(OPTIONAL_APIS)) + r')\s*\(')
PYTHON_SCOPES = ('src', 'dualpc', 'workshop/bridge', 'vendor/ok-script/ok')
PYTHON_NEW_METHODS = {'removeprefix', 'removesuffix', 'is_relative_to', 'with_stem', 'file_digest'}


def optional_api_calls(text: str) -> list[dict]:
    cleaned = CPP_IGNORED.sub(lambda match: '\n' * match.group().count('\n'), text)
    return [dict(api=match[1], line=cleaned.count('\n', 0, match.start()) + 1)
            for match in API_CALL.finditer(cleaned)]


def audit(root: Path) -> dict:
    calls = []
    for scope in ('native/platform', 'native/include', 'native/host', 'native/ui'):
        for path in sorted((root / scope).rglob('*')):
            if path.suffix in {'.cpp', '.hpp', '.h'}:
                calls.extend(dict(path=path.relative_to(root).as_posix(), **entry)
                             for entry in optional_api_calls(path.read_text(encoding='utf-8-sig')))
    syntax_errors, reviews = [], []
    python_files = 0
    for scope in PYTHON_SCOPES:
        for path in sorted((root / scope).rglob('*.py')):
            python_files += 1
            name = path.relative_to(root).as_posix()
            text = path.read_text(encoding='utf-8-sig')
            try:
                ast.parse(text, filename=name, feature_version=(3, 8))
            except SyntaxError as error:
                syntax_errors.append(dict(path=name, line=error.lineno, reason=error.msg))
            tree = ast.parse(text, filename=name)
            for node in ast.walk(tree):
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr in PYTHON_NEW_METHODS:
                    reviews.append(dict(path=name, line=node.lineno, feature=node.func.attr))
                if isinstance(node, ast.ImportFrom) and (node.module or '').startswith('PySide6'):
                    reviews.append(dict(path=name, line=node.lineno, feature='PySide6'))
                if isinstance(node, ast.BinOp) and isinstance(node.op, ast.BitOr) and isinstance(node.left, ast.Dict):
                    reviews.append(dict(path=name, line=node.lineno, feature='dict_union'))
    cmake = (root / 'native/CMakeLists.txt').read_text(encoding='utf-8-sig')
    qt = re.search(r'find_package\(Qt(\d+)\s+([\d.]+)', cmake)
    rust = re.search(r'set\(OKWW_RUST_TARGET\s+"([^"]+)"',
                     (root / 'native/cmake/RustCore.cmake').read_text(encoding='utf-8-sig'))
    python_requires = {}
    for name in ('pyproject.toml', 'vendor/ok-script/pyproject.toml'):
        python_requires[name] = tomllib.loads((root / name).read_text(encoding='utf-8-sig'))['project'].get('requires-python')
    requirements = (root / 'requirements-dualpc.txt').read_text(encoding='utf-8-sig')
    inference = re.search(r'^openvino==([^\s]+)', requirements, re.M)
    return dict(
        status='porting_required', runtime_tested_on_win7=False,
        scope='source audit only; does not inspect every transitive binary dependency',
        qt_requirement=qt.group(0) if qt else 'manual review required',
        rust_target=rust[1] if rust else 'manual review required',
        python_requires=python_requires,
        openvino_version=inference[1] if inference else 'manual review required',
        direct_optional_api_calls=calls, python_files_scanned=python_files,
        python_38_syntax_errors=syntax_errors, python_legacy_reviews=reviews,
        required_work=['Qt legacy build', 'Rust Win7 standard library', 'Win7 inference backend',
                       'Python workshop runtime', 'capture capabilities', 'separate update assets',
                       'complete-package testing on Windows 7 SP1 x64'],
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check-optional-apis', action='store_true',
                        help='Fail on direct calls to the listed newer APIs; this does not certify Win7 support')
    args = parser.parse_args()
    result = audit(ROOT)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    summary = {key: result[key] for key in ('status', 'runtime_tested_on_win7', 'qt_requirement',
               'rust_target', 'python_requires', 'openvino_version', 'python_files_scanned')}
    summary.update(direct_optional_api_calls=result['direct_optional_api_calls'],
                   python_38_syntax_errors=len(result['python_38_syntax_errors']),
                   python_legacy_reviews=len(result['python_legacy_reviews']))
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 1 if args.check_optional_apis and result['direct_optional_api_calls'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
