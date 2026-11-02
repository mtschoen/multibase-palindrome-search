"""Independent enumeration and conservative rejection checks (stdlib only)."""

import contextlib
import io
import itertools
import json
import runpy
import sys
import unittest
from unittest.mock import patch

from experiments import exact_pruning as pruning


def direct_candidates(length, leading_bits):
    """Mirror unrestricted hex halves, then filter by binary reversal."""
    for half in itertools.product(range(16), repeat=(length + 1) // 2):
        if half[0].bit_length() != leading_bits:
            continue
        digits = half + half[:length // 2][::-1]
        value = int(''.join(format(digit, 'x') for digit in digits), 16)
        binary = format(value, 'b')
        if binary == binary[::-1]:
            yield value


class ExactPruningTests(unittest.TestCase):
    def test_bucket_boundaries_strengthen_rejection_without_losing_witnesses(self):
        predicate = pruning.bucket_interval_rejects
        for lower, upper in ((1900, 2099), (9800, 10020), (9900, 10099)):
            with self.subTest(lower=lower, upper=upper):
                self.assertFalse(pruning.interval_rejects(lower, upper, 0, 4))
                self.assertTrue(predicate(lower, upper, 0, 4))
        for lower, upper, value, bits in ((1200, 1299, 3, 1),
                                          (0, 0, 0, 8), (0, 10, 0, 8),
                                          (10, 10000, 0, 8),
                                          (120, 122, 1, 8), (1220, 1222, 1, 8)):
            self.assertFalse(predicate(lower, upper, value, bits))

    def test_search_matches_independent_exhaustion(self):
        for length in range(1, 9):
            for leading_bits in range(1, 5):
                with self.subTest(length=length, leading_bits=leading_bits):
                    candidates = list(direct_candidates(length, leading_bits))
                    endpoints, inner_digits = pruning.GROUPS[leading_bits]
                    weights = pruning.mirrored_weights(length)
                    constructed = {sum(digit * weight for digit, weight in zip(digits, weights))
                                   for digits in itertools.product(endpoints,
                                       *([inner_digits] * (len(weights) - 1)))}
                    self.assertEqual(constructed, set(candidates))
                    expected_hits = sorted(value for value in candidates
                                           if str(value) == str(value)[::-1])
                    for method in ('common', 'buckets'):
                        result = pruning.search(length, leading_bits, pruner=method)
                        self.assertEqual(result.pruner, method)
                        self.assertEqual(result.hits, expected_hits)
                        self.assertEqual(result.assignments, len(candidates))
                        self.assertEqual(result.covered_assignments, len(candidates))
                        self.assertEqual(result.examined_leaves + result.pruned_assignments,
                                         len(candidates))
                        self.assertTrue(result.completed)
        self.assertEqual(pruning.search(1, 4).hits, [9])
        self.assertGreater(pruning.search(6, 4).pruned_branches, 0)

    def test_interval_rejections_never_remove_decimal_palindromes(self):
        intervals = [(lower, upper) for lower in range(81)
                     for upper in range(lower, 101)]
        intervals += [(lower, lower + width) for lower in range(100, 140)
                      for width in (0, 1, 8, 19)]
        intervals += [(10 ** exponent - 12, 10 ** exponent + 12)
                      for exponent in range(2, 30)]
        for lower, upper in intervals:
            palindromes = [value for value in range(lower, upper + 1)
                           if str(value) == str(value)[::-1]]
            for fixed_bits in (0, 1, 2, 4, 8, 16):
                for value in range(1 << min(fixed_bits, 4)):
                    for predicate in (pruning.interval_rejects, pruning.bucket_interval_rejects):
                        if not predicate(lower, upper, value, fixed_bits):
                            continue
                        modulus = 1 << fixed_bits
                        self.assertFalse(any(candidate % modulus == value % modulus
                                             for candidate in palindromes),
                                         (lower, upper, value, fixed_bits))
        self.assertTrue(pruning.interval_rejects(1200, 1299, 0, 4))
        self.assertFalse(pruning.interval_rejects(999, 1001, 0, 16))
        self.assertFalse(pruning.interval_rejects(1200, 1299, 1, 1))
        # Every palindrome witness survives, including odd/even digit lengths,
        # zero, and powers of ten through decimal lengths beyond machine words.
        witnesses = [value for value in range(10000) if str(value) == str(value)[::-1]]
        witnesses += [10 ** exponent + 1 for exponent in range(4, 30)]
        for witness in witnesses:
            for width in (0, 1, 19, 1000):
                for fixed_bits in (0, 1, 2, 4, 8, 16):
                    self.assertFalse(pruning.bucket_interval_rejects(
                        max(0, witness - width), witness + width, witness, fixed_bits),
                        (witness, width, fixed_bits))

    def test_every_small_partial_interval_contains_its_completions(self):
        for length in range(1, 7):
            for leading_bits in range(1, 5):
                candidates = list(direct_candidates(length, leading_bits))
                for depth in range(1, (length + 1) // 2 + 1):
                    buckets = {}
                    for value in candidates:
                        prefix = format(value, 'x')[:depth]
                        buckets.setdefault(prefix, []).append(value)
                    weights = pruning.mirrored_weights(length)
                    for prefix, completions in buckets.items():
                        value = sum(int(digit, 16) * weight
                                    for digit, weight in zip(prefix, weights))
                        upper = value + max(pruning.GROUPS[leading_bits][1]) * sum(weights[depth:])
                        self.assertEqual(value, min(completions))
                        self.assertEqual(upper, max(completions))
                        fixed_bits = min(4 * depth, 4 * length)
                        self.assertEqual({candidate % (1 << fixed_bits)
                                          for candidate in completions},
                                         {value % (1 << fixed_bits)})
                        for predicate in (pruning.interval_rejects, pruning.bucket_interval_rejects):
                            if predicate(value, upper, value, fixed_bits):
                                self.assertFalse(any(str(candidate) == str(candidate)[::-1]
                                                     for candidate in completions))

    def test_cli_selection_and_validation(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(pruning.main(['1', '2', '--leading-bits', '1', '4']), 0)
        rows = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual([(row['length'], row['leading_bits']) for row in rows],
                         [(1, 1), (1, 4), (2, 1), (2, 4)])
        self.assertTrue(all(row['completed'] for row in rows))
        self.assertTrue(all(row['pruner'] == 'common' for row in rows))
        with contextlib.redirect_stdout(io.StringIO()) as bucket_output:
            self.assertEqual(pruning.main(['6', '6', '--pruner', 'buckets']), 0)
        self.assertTrue(all(json.loads(line)['pruner'] == 'buckets'
                            for line in bucket_output.getvalue().splitlines()))
        with patch.object(sys, 'argv', [pruning.__file__, '1', '1']):
            with contextlib.redirect_stdout(io.StringIO()) as script_output:
                with self.assertRaises(SystemExit) as script_exit:
                    runpy.run_path(pruning.__file__, run_name='__main__')
        self.assertEqual(script_exit.exception.code, 0)
        self.assertEqual([json.loads(line)['hits']
                          for line in script_output.getvalue().splitlines()],
                         [[1], [3], [5, 7], [9]])
        for arguments in (['0', '2'], ['3', '2'], ['1', '1', '--budget-seconds', '0'],
                          ['1', '1', '--pruner', 'unknown']):
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    pruning.main(arguments)
            self.assertEqual(error.exception.code, 2)
        for length, leading_bits in ((0, 1), (1, 0), (1, 5)):
            with self.assertRaises(ValueError):
                pruning.search(length, leading_bits)
        with self.assertRaises(ValueError):
            pruning.search(1, 1, budget_seconds=0)
        with self.assertRaises(ValueError):
            pruning.search(1, 1, pruner='unknown')

    def test_budget_reports_incomplete_coverage_without_claiming_exhaustion(self):
        for method in ('common', 'buckets'):
            with self.subTest(pruner=method):
                with patch.object(pruning, 'monotonic_ns', side_effect=[0, 2_000_000_000,
                                                                      2_000_000_000]):
                    result = pruning.search(20, 4, budget_seconds=1, pruner=method)
                self.assertFalse(result.completed)
                self.assertLess(result.covered_assignments, result.assignments)
                with patch.object(pruning, 'monotonic_ns', side_effect=[0, 0, 2_000_000_000,
                                                                      2_000_000_000]):
                    partial = pruning.search(32, 3, budget_seconds=1, pruner=method)
                self.assertFalse(partial.completed)
                self.assertGreater(partial.covered_assignments, 0)
                self.assertLess(partial.covered_assignments, partial.assignments)
                with patch.object(pruning, 'monotonic_ns', side_effect=[0, 2_000_000_000,
                                                                      2_000_000_000]):
                    with contextlib.redirect_stdout(io.StringIO()) as output:
                        status = pruning.main(['20', '20', '--leading-bits', '4',
                                               '--budget-seconds', '1', '--pruner', method])
                self.assertEqual(status, 1)
                self.assertFalse(json.loads(output.getvalue())['completed'])


if __name__ == '__main__':
    unittest.main()
