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


@dataclass
class SearchResult:
    length: int
    leading_bits: int
    assignments: int
    visited_nodes: int = 0
    pruned_branches: int = 0
    pruned_assignments: int = 0
    examined_leaves: int = 0
    covered_assignments: int = 0
    completed: bool = True
    elapsed_nanoseconds: int = 0
    hits: list = field(default_factory=list)


def search(length, leading_bits, budget_seconds=None):
    """Search one group; a budget is a cooperative limit checked per 4096 nodes.

    Covered assignments counts disjoint pruned subtrees plus exact leaf checks.
    An interrupted search reports completed=False and never claims exhaustion.
    """
    if length < 1 or leading_bits not in GROUPS:
        raise ValueError('positive hex length and leading bits 1 through 4 required')
    if budget_seconds is not None and budget_seconds <= 0:
        raise ValueError('budget must be positive')
    endpoints, inner_digits = GROUPS[leading_bits]
    weights = mirrored_weights(length)
    remaining_maximum = [max(inner_digits) * sum(weights[depth:])
                         for depth in range(len(weights) + 1)]
    subtree_counts = [len(inner_digits) ** (len(weights) - depth)
                      for depth in range(len(weights) + 1)]
    result = SearchResult(length, leading_bits,
                          len(endpoints) * subtree_counts[1])
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
        if interval_rejects(value, value + remaining_maximum[depth], value, 4 * depth):
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
    options = parser.parse_args(arguments)
    if options.minimum_length < 1 or options.maximum_length < options.minimum_length:
        parser.error('require 1 <= minimum_length <= maximum_length')
    if options.budget_seconds is not None and options.budget_seconds <= 0:
        parser.error('budget seconds must be positive')
    for length in range(options.minimum_length, options.maximum_length + 1):
        for leading_bits in sorted(set(options.leading_bits)):
            result = search(length, leading_bits, options.budget_seconds)
            print(json.dumps(asdict(result), sort_keys=True), flush=True)
            if not result.completed:
                return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
