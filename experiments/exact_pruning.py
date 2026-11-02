"""Experimental exact outside-in binary/hex palindrome search, stdlib only.

Run: python -B experiments/exact_pruning.py 20 24 --leading-bits 3 4
Each output line is a JSON summary for one (hex length, leading bits) group.
"""

import argparse
from dataclasses import asdict, dataclass, field
import json
from time import monotonic_ns


# Endpoints, then allowable inner digits, indexed by significant endpoint bits.
GROUPS = {
    1: ((1,), (0, 1)),
    2: ((3,), (0, 3)),
    3: ((5, 7), (0, 2, 5, 7)),
    4: ((9, 15), (0, 6, 9, 15)),
}


def mirrored_weights(length):
    """One weight per independent digit; the odd center occurs exactly once."""
    return tuple((1 << (4 * position)) + (1 << (4 * (length - 1 - position)))
                 if position != length - 1 - position else 1 << (4 * position)
                 for position in range((length + 1) // 2))


def interval_rejects(lower, upper, fixed_value, fixed_bits):
    """Reject only a necessary decimal suffix / fixed binary residue mismatch.

    Caller guarantees all completions lie in [lower, upper] and have residue
    fixed_value modulo 2**fixed_bits. Crossing a decimal length boundary gives
    no usable prefix. Strings perform exact integer digit extraction only.
    """
    lower_digits = str(lower)
    upper_digits = str(upper)
    if len(lower_digits) != len(upper_digits):
        return False
    maximum_digits = min(fixed_bits, len(lower_digits) // 2)
    common_digits = 0
    while (common_digits < maximum_digits
           and lower_digits[common_digits] == upper_digits[common_digits]):
        common_digits += 1
    if common_digits == 0:
        return False
    required_suffix = int(lower_digits[:common_digits][::-1])
    mask = (1 << common_digits) - 1
    return (required_suffix & mask) != (fixed_value & mask)


def _bucket_piece_rejects(lower, upper, fixed_value, fixed_bits, decimal_digits):
    """Check at most four prefixes of one decimal digit length."""
    maximum_digits = min(fixed_bits, decimal_digits // 2)
    width = upper - lower
    count = (maximum_digits if width == 0 else
             min(maximum_digits, decimal_digits - len(str(width))))
    divisor = 10 ** (decimal_digits - count)
    if count < maximum_digits:
        extended_divisor = divisor // 10
        if upper // extended_divisor - lower // extended_divisor < 4:
            count += 1
            divisor = extended_divisor
    if count == 0:
        return False
    # Width-based selection guarantees <=2 prefixes before the one extension.
    # A second extension would span at least ten prefixes, so cannot help.
    mask = (1 << count) - 1
    residue = fixed_value & mask
    for prefix in range(lower // divisor, upper // divisor + 1):
        if int(str(prefix)[::-1]) & mask == residue:
            return False
    return True


def bucket_interval_rejects(lower, upper, fixed_value, fixed_bits):
    """Conservative prefix-bucket rejection for nonnegative integer intervals.

    As with interval_rejects, callers supply ordered bounds and nonnegative bits.
    Split one decimal-length boundary; wider intervals conservatively pass.
    Each piece enumerates at most four prefixes, capped at half its digit count
    and the fixed bit count. A matching residue is necessary, not sufficient.
    """
    if interval_rejects(lower, upper, fixed_value, fixed_bits):
        return True
    lower_length = len(str(lower))
    upper_length = len(str(upper))
    if upper_length == lower_length:
        return _bucket_piece_rejects(lower, upper, fixed_value, fixed_bits, lower_length)
    if upper_length != lower_length + 1:
        return False
    boundary = 10 ** lower_length
    return (_bucket_piece_rejects(lower, boundary - 1, fixed_value, fixed_bits, lower_length)
            and _bucket_piece_rejects(boundary, upper, fixed_value, fixed_bits, upper_length))


PRUNERS = {'common': interval_rejects, 'buckets': bucket_interval_rejects}


@dataclass
class SearchResult:
    length: int
    leading_bits: int
    assignments: int
    pruner: str = 'common'
    visited_nodes: int = 0
    pruned_branches: int = 0
    pruned_assignments: int = 0
    examined_leaves: int = 0
    covered_assignments: int = 0
    completed: bool = True
    elapsed_nanoseconds: int = 0
    hits: list = field(default_factory=list)


def search(length, leading_bits, budget_seconds=None, pruner='common'):
    """Search one group; a budget is a cooperative limit checked per 4096 nodes.

    Covered assignments counts disjoint pruned subtrees plus exact leaf checks.
    An interrupted search reports completed=False and never claims exhaustion.
    """
    if length < 1 or leading_bits not in GROUPS:
        raise ValueError('positive hex length and leading bits 1 through 4 required')
    if budget_seconds is not None and budget_seconds <= 0:
        raise ValueError('budget must be positive')
    if pruner not in PRUNERS:
        raise ValueError('pruner must be common or buckets')
    rejects = PRUNERS[pruner]
    endpoints, inner_digits = GROUPS[leading_bits]
    weights = mirrored_weights(length)
    remaining_maximum = [max(inner_digits) * sum(weights[depth:])
                         for depth in range(len(weights) + 1)]
    subtree_counts = [len(inner_digits) ** (len(weights) - depth)
                      for depth in range(len(weights) + 1)]
    result = SearchResult(length, leading_bits,
                          len(endpoints) * subtree_counts[1], pruner=pruner)
    start = monotonic_ns()
    deadline = None if budget_seconds is None else start + budget_seconds * 10**9

    def visit(depth, value):
        result.visited_nodes += 1
        if deadline is not None and result.visited_nodes % 4096 == 1:
            if monotonic_ns() >= deadline:
                result.completed = False
                return
        if depth == len(weights):
            result.examined_leaves += 1
            decimal_digits = str(value)
            if decimal_digits == decimal_digits[::-1]:
                result.hits.append(value)
            return
        if rejects(value, value + remaining_maximum[depth], value, 4 * depth):
            result.pruned_branches += 1
            result.pruned_assignments += subtree_counts[depth]
            return
        for digit in inner_digits:
            visit(depth + 1, value + digit * weights[depth])
            if not result.completed:
                return

    # The root chooses nonzero endpoints; all remaining digits include zero.
    for endpoint in endpoints:
        visit(1, endpoint * weights[0])
        if not result.completed:
            break
    result.elapsed_nanoseconds = monotonic_ns() - start
    result.covered_assignments = result.pruned_assignments + result.examined_leaves
    result.hits.sort()
    if result.completed:
        assert result.covered_assignments == result.assignments
    return result


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('minimum_length', type=int)
    parser.add_argument('maximum_length', type=int)
    parser.add_argument('--leading-bits', type=int, nargs='+', choices=range(1, 5),
                        default=[1, 2, 3, 4])
    parser.add_argument('--budget-seconds', type=int,
                        help='cooperative time limit per group; incomplete exits 1')
    parser.add_argument('--pruner', choices=PRUNERS, default='common')
    options = parser.parse_args(arguments)
    if options.minimum_length < 1 or options.maximum_length < options.minimum_length:
        parser.error('require 1 <= minimum_length <= maximum_length')
    if options.budget_seconds is not None and options.budget_seconds <= 0:
        parser.error('budget seconds must be positive')
    for length in range(options.minimum_length, options.maximum_length + 1):
        for leading_bits in sorted(set(options.leading_bits)):
            result = search(length, leading_bits, options.budget_seconds, options.pruner)
            print(json.dumps(asdict(result), sort_keys=True), flush=True)
            if not result.completed:
                return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
