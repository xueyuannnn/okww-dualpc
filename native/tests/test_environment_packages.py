"""Release packaging invariants; fixtures contain no real runtimes or profiles."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools_release'))
from prepare_environment import environment_manifest, make_environment, write_cumulative, UPDATE_NAME
from verify_package import verify


def entry(path, data):
    return dict(path=path, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


class EnvironmentPackageTests(unittest.TestCase):
    def test_fixed_environment_and_single_cumulative_package(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); base = root / 'base'; stage = root / 'app'; review = root / 'review'
            base.mkdir(); stage.mkdir(); review.mkdir()
            files = {'workshop-engine/runtime/python.exe': b'python', 'Qt6Core.dll': b'qt',
                     'assets/ocr/rec.onnx': b'model', 'OKWW-DualPC-Native-Dev.exe': b'old app',
                     'workshop-engine/src/test.py': b'old code'}
            for n, b in files.items():
                p = base / n; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(b)
            manifest = dict(variant='full', release_version='0.1.0', personal_profiles=0,
                            files=[entry(n, b) for n, b in files.items()])
            (base / 'PACKAGE.json').write_text(json.dumps(manifest))
            environment, archive = make_environment(base, review)
            self.assertTrue(verify(archive)['ok'])
            names = {e['path'] for e in environment['files']}
            self.assertNotIn('OKWW-DualPC-Native-Dev.exe', names)
            self.assertIn('assets/ocr/rec.onnx', names)
            (stage / 'ENVIRONMENT.json').write_text(json.dumps(environment))
            (stage / 'OKWW-DualPC-Native-Dev.exe').write_bytes(b'current app')
            # A script reverted to the base contents must still be in the archive.
            (stage / 'script.py').write_bytes(b'old code')
            update = write_cumulative(stage, environment, '0.3.0', review)
            self.assertEqual(update.name, UPDATE_NAME)
            self.assertTrue(verify(update)['cumulative'])
            with zipfile.ZipFile(update) as zipped:
                self.assertIn('OKWW-DualPC-full/script.py', zipped.namelist())
                self.assertNotIn('OKWW-DualPC-full/Qt6Core.dll', zipped.namelist())
            before = environment['environment_id']
            manifest['release_version'] = '0.2.0'
            self.assertEqual(environment_manifest(manifest)['environment_id'], before)
            manifest['files'][0] = entry('workshop-engine/runtime/python.exe', b'new python')
            self.assertNotEqual(environment_manifest(manifest)['environment_id'], before)
            (stage / 'Qt6Core.dll').write_bytes(b'changed runtime')
            with self.assertRaisesRegex(RuntimeError, 'new environment'):
                write_cumulative(stage, environment, '0.4.0', review)

    def test_private_base_is_not_a_public_environment(self):
        with self.assertRaises(RuntimeError):
            environment_manifest(dict(variant='personal', personal_profiles=1, files=[]))


if __name__ == '__main__':
    unittest.main()
