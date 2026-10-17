"""Offline build identity tests; run on the build box."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cyd_build', ROOT / 'tools/cyd_build.py')
assert spec is not None and spec.loader is not None
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class IdentityTests(unittest.TestCase):
    def test_archive_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(build.revision(directory), 'unknown')

    def test_clean_dirty_and_ignored(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def git(*args):
                return subprocess.check_output(['git', '-C', directory, *args], stderr=subprocess.DEVNULL, text=True).strip()
            git('init')
            (root / '.gitignore').write_text('generated.h\n')
            git('add', '.gitignore')
            git('-c', 'user.name=Test', '-c', 'user.email=' + 'test' + '@' + 'example.invalid', 'commit', '-m', 'fixture')
            rev = git('rev-parse', '--short', 'HEAD')
            self.assertEqual(build.revision(root), rev)
            (root / 'generated.h').write_text('ignored')
            self.assertEqual(build.revision(root), rev)
            (root / 'new.txt').write_text('untracked')
            self.assertEqual(build.revision(root), rev + '-dirty')
            (root / 'new.txt').unlink()
            (root / '.gitignore').write_text('generated.h\nother\n')
            self.assertEqual(build.revision(root), rev + '-dirty')
            git('add', '.gitignore')
            self.assertEqual(build.revision(root), rev + '-dirty')


if __name__ == '__main__':
    unittest.main()
