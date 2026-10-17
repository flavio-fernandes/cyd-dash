import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class TapTests(unittest.TestCase):
    def test_actual_cpp_model(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / 'tap-test'
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(ROOT / 'firmware/core'),
                            str(ROOT / 'tests/tap_model_test.cpp'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
