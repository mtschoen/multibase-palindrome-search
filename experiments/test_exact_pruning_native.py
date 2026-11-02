"""Compile once and compare the native experiment with exact integer oracles."""

from dataclasses import asdict
import json
import os
from pathlib import Path
import subprocess
import unittest

from experiments import exact_pruning


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'experiments' / 'exact_pruning_native.c'
EXECUTABLE = ROOT / 'bin' / 'exact_pruning_native.exe'
# A hang guard, never an assertion about execution speed.
TIMEOUT = float(os.environ.get('NATIVE_PRUNING_TIMEOUT_SECONDS', '180'))


class NativePruningTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if SOURCE.exists():
            EXECUTABLE.parent.mkdir(exist_ok=True)
            compilation = subprocess.run(
                ['clang', '-O3', '-Wall', '-Wextra', '--rtlib=compiler-rt',
                 '-o', str(EXECUTABLE), str(SOURCE)],
                capture_output=True, text=True, timeout=TIMEOUT, check=True)
            if compilation.stderr:
                raise AssertionError(compilation.stderr)

    def run_native(self, *arguments):
        self.assertTrue(SOURCE.exists(), 'native exact-pruning implementation is required')
        return subprocess.run([str(EXECUTABLE), *map(str, arguments)],
                              capture_output=True, text=True, timeout=TIMEOUT)

    def test_reference_accounting(self):
        for length in [*range(1, 13), 16, 20, 24, 28, 32]:
            process = self.run_native(length, length)
            self.assertEqual(process.returncode, 0, process.stderr)
            rows = [json.loads(line) for line in process.stdout.splitlines()]
            self.assertEqual(len(rows), 4)
            for row in rows:
                with self.subTest(length=length, leading_bits=row['leading_bits']):
                    expected = asdict(exact_pruning.search(length, row['leading_bits']))
                    self.assertEqual(expected.pop('pruner'), 'common')
                    expected.pop('elapsed_nanoseconds')
                    row.pop('elapsed_nanoseconds', None)
                    self.assertEqual(row, expected)

    def test_positive_hits_and_selection(self):
        process = self.run_native(1, 2, '--leading-bits', 4, 3, 2, 1, 4)
        self.assertEqual(process.returncode, 0, process.stderr)
        rows = [json.loads(line) for line in process.stdout.splitlines()]
        self.assertEqual([(row['length'], row['leading_bits']) for row in rows],
                         [(length, bits) for length in (1, 2) for bits in range(1, 5)])
        self.assertEqual([value for row in rows for value in row['hits']], [1, 3, 5, 7, 9])

    def test_invalid_cli(self):
        for arguments in [(), (0, 2), (3, 2), (1, 65), ('x', 2), (1, '2x'),
                          (1, 1, '--leading-bits'), (1, 1, '--leading-bits', 5),
                          (1, 1, '--unknown'), (1, 1, '--leading-bits', 0)]:
            with self.subTest(arguments=arguments):
                process = self.run_native(*arguments)
                self.assertEqual(process.returncode, 2)
                self.assertEqual(process.stdout, '')
                self.assertTrue(process.stderr)

    def test_full_width_groups(self):
        process = self.run_native(64, 64, '--leading-bits', 1, 2)
        self.assertEqual(process.returncode, 0, process.stderr)
        rows = [json.loads(line) for line in process.stdout.splitlines()]
        self.assertEqual([row['leading_bits'] for row in rows], [1, 2])
        for row in rows:
            expected = asdict(exact_pruning.search(64, row['leading_bits']))
            self.assertEqual(expected.pop('pruner'), 'common')
            expected.pop('elapsed_nanoseconds')
            row.pop('elapsed_nanoseconds', None)
            self.assertEqual(row, expected)
            self.assertEqual(row['covered_assignments'], 2 ** 31)

    def test_decimal_conversion_independent_witnesses(self):
        self.assertTrue(SOURCE.exists(), 'native exact-pruning implementation is required')
        # Test translation unit only: no production flags or exported test seams.
        witness_source = EXECUTABLE.parent / 'native_decimal_witness.c'
        witness_executable = EXECUTABLE.parent / 'native_decimal_witness.exe'
        witness_source.write_text(
            '#define main native_search_main\n'
            '#include "../experiments/exact_pruning_native.c"\n'
            '#undef main\n'
            'int main(void) { struct wide value; char digits[79];\n'
            'while (scanf("%" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64,\n'
            ' &value.limb[3], &value.limb[2], &value.limb[1], &value.limb[0]) == 4) {\n'
            ' decimal_digits(value, digits); puts(digits); } return 0; }\n',
            encoding='ascii')
        try:
            subprocess.run(['clang', '-O3', '-Wall', '-Wextra', '--rtlib=compiler-rt',
                            '-o', str(witness_executable), str(witness_source)],
                           check=True, capture_output=True, text=True, timeout=TIMEOUT)
            values = [0, 1, 2**256 - 1, 2**252 + 1]
            values += [2**bits + offset for bits in (64, 128, 192, 255)
                       for offset in (-1, 0, 1)]
            values += [10**power + offset for power in (19, 38, 57, 77)
                       for offset in (-1, 0, 1)]
            data = ''.join(' '.join(f'{value >> shift & (2**64 - 1):x}'
                                   for shift in (192, 128, 64, 0)) + '\n'
                           for value in values)
            process = subprocess.run([str(witness_executable)], input=data,
                                     text=True, capture_output=True, timeout=TIMEOUT, check=True)
            self.assertEqual(process.stdout.splitlines(), [str(value) for value in values])
        finally:
            witness_source.unlink(missing_ok=True)


if __name__ == '__main__':
    unittest.main()
