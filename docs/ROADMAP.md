# Roadmap

The 2026-11-02 Astra-led audit changed the practical next step: exact outside-in
pruning is now implemented and substantially reduces the work. See
[the audit narrative](AUDIT-2026-11-02.md), [results](RESULTS.md), and
[the pruning proof](VERIFICATION.md#exact-pruning-proof-and-independent-checks).
The problem remains open beyond the explicitly completed ranges.

## 1. Improve the exact search before extending enumeration

`experiments/exact_pruning.py` is the arbitrary-precision reference;
`experiments/exact_pruning_native.c` implements the same common-prefix rule
for up to 64 hexadecimal digits. Their tests compare node counts, branch
counts, leaf counts, covered assignments and hits, not merely zero hits.

The first Python run covered all four families through 56 hex digits, then
stopped partway through the length-57, leading-width-3 group at its cooperative
120-second budget. That incomplete group establishes no bound. The native port
reduces interpreter overhead. Its completed bounds are recorded separately.

Useful next experiments:

- Profile exact decimal conversion and traversal in the native implementation.
  Preserve agreement with the Python reference before claiming a new bound.
- Partition by disjoint outer prefixes for parallel CPU execution. Record the
  actual prefixes and completed groups so gaps cannot hide behind a grand total.
- Explore reachable suffix residues modulo powers of five. Unassigned hex digits
  affect these residues, so an internal node cannot compare its partial value
  directly with the required decimal suffix modulo `5^k`.
- Consider independently checkable certificates for rejected subtrees. A solver
  or tree certificate covers only its finite ranges unless another theorem
  supplies a global length bound.

An optional prefix-bucket predicate considers several possible decimal prefixes
when an interval straddles a prefix boundary. At the measured lengths it visited
10 to 11 percent fewer nodes but took 29 to 35 percent longer. It remains an
optional research comparison; `common` is the default. See
[the measurements](../experiments/BUCKET-PRUNING.md).

## 2. Meet in the middle remains an experiment

Fixing outer orbits can determine a decimal prefix and the low binary bits.
First reject disagreement between the reversed prefix and the fixed value
modulo `2^k`. The remaining necessary condition modulo `5^k` is a subset-sum
condition on the free orbit masks.

Split the remaining masks, retain every assignment sharing a residue, and reuse
the tables across outer assignments. Exact integer intervals must handle
ambiguous prefixes; a fixed number of high bits does not invariably determine
the same number of decimal digits.

The earlier roadmap's `2^(f/2)` estimate was per outer assignment. With `u`
outer choices and `f` inner choices, a simple implementation also pays for up
to `2^u` outer assignments and for matching pairs. Under a uniform-residue
heuristic, there are about `2^f / 5^k` pairs per outer assignment. Table reuse
and early modulo-`2^k` rejection can help, but neither the heuristic nor the
projected desktop reach is a proved complexity bound. Benchmark a complete
implementation against exact pruning before claiming an advantage.

## 3. The GPU enumerator remains an independent check

The corrected GPU implementation independently checks the pruned search. Its
survivor counter is wide enough for every configured launch, and its overflow
splitting has a real-kernel regression.

Enumeration still visits about `2^L` candidates at hex length L. The old
projections of days or years describe that algorithm, not a lower bound on the
cost of the mathematical problem. New multi-day runs should be justified by
what they add beyond exact pruning and independent checks.

## 4. What a nonexistence proof must add

The orbit/alphabet construction and pruning rejection rule are proved for
arbitrary mathematical lengths. Neither says that every length has no survivor.
A global proof still needs a uniform contradiction, a proved finite length
bound, or another argument covering every unsearched length.

Even-length decimal palindromes are divisible by 11, even-bit binary
palindromes by 3, and even-length hex palindromes by 17. These necessary
conditions do not by themselves conflict.

A fixed residue-only sieve has a specific limitation. For any modulus M and
base b, write `M = u*v`, where the prime factors of u divide b and `gcd(b,v)=1`.
Choose arbitrarily large t with `u | b^t` and `b^t = 1 mod v`. Then
`1 + b^t + ... + b^(v*t)` is a base-b palindrome congruent to 1 modulo M.
For b=16 it is also binary-palindromic; b=10 gives decimal palindromes in the
same residue. Thus comparing the possible residues of those two families
separately modulo any fixed finite collection of moduli cannot separate them.
Length-dependent or combined digit/carry arguments remain possible.

The random-digit model gives about `2^(-L)` expected hits at hex length L,
up to length- and endpoint-dependent factors. This motivates expecting more
empty searches, but gives neither a rigorous probability of existence nor a
proof that the solution set is empty.

Goins' 2009 theorem concerns equal digit counts in two bases; the unrestricted
representations here have different lengths. Berczes and Ziegler's 2014 work
bounds special structured families and does not settle this three-base problem.
Their papers are linked from the README. Carry analysis and exact constraints
remain research tools, not a completed proof or a promise that a proof is close.
