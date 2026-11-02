# Roadmap

Three directions: push the brute-force bound, change the algorithm so the bound
moves by more than a few hex digits, and think about a proof. Each section says
what the obstacle is and what a contribution would look like.

## 1. Push the bound with the current algorithm

The work doubles per hex digit. On a 16-core desktop at 8e8 candidates per
second, hex lengths 39 and 40 cost about 23 minutes each, 41 and 42 together
about 1.5 hours, and 47 and 48 together about 4 days. The search is
embarrassingly parallel: a work unit is (hex length, leading bits, index range),
and `tripal_wide` already splits groups into contiguous index slices, so
distributing across machines is a matter of handing out ranges and collecting
`done:` lines.

Concrete steps:

- Add an index-range argument so a run can take a slice of one group instead of
  a whole hex-length range. This turns the program into a worker.
- Record completed (group, range) pairs in a results file in this repository
  with machine and timing, so coverage can be audited.
- The GPU port exists (`src/tripal_cuda.cu`) and runs about 70 times faster
  than the 16-core CPU on one RTX 4090: 5.7e10 candidates per second. Hex
  length 39 or 40 takes about 20 s, 47 and 48 together about 80 min, 55 and 56
  about 2 weeks, 63 and 64 about 10 years. Multiple GPUs scale linearly since
  work units are independent.

Honest expectation: see section 3. The heuristic says each new hex length has
about a 2^-L chance of containing a solution.

## 2. Change the exponent: meet in the middle

The current cost is 2^L per hex length because every free orbit is enumerated.
The decimal condition has structure the enumeration ignores.

Observation: the top k decimal digits of a candidate are determined by its top
roughly 3.32k bits, and the binary palindrome condition ties the top bits to the
bottom bits, so fixing the outer orbits fixes both the leading decimal digits
and the value mod 2^m. The decimal palindrome condition then demands that the
value mod 10^k equal the reversal of those leading digits, a specific residue r.
Since the value mod 2^k is already fixed, the real constraint is on the value
mod 5^k, which depends on all orbits.

That constraint is linear in the orbit choices: each free orbit contributes its
mask's residue mod 5^k when set. So for each assignment of the outer orbits, the
inner orbits must sum to a target residue mod 5^k. Split the inner orbits into
two halves, tabulate the residues of all assignments of one half in a hash map,
and look up each assignment of the other half. Cost per outer assignment is
about 2^(f/2) instead of 2^f for f inner orbits. Choosing k so that 5^k is
comparable to 2^(f/2) keeps the candidate list small, and each survivor still
gets the full decimal check.

This would make hex lengths in the 60s reachable on a desktop. The design needs
care: the Gray-code trick no longer applies, the outer and inner split must be
chosen per (L, b) group, and the hash tables must be rebuilt or reused across
outer assignments. A standalone prototype with its pair output checked against
`tripal_wide pairs` is the right first step.

Regan noted that Charlton Harrison's binary-and-decimal palindrome search used
pruning rules that eliminate swaths of candidates, and asked whether such rules
exist here. The modular constraint above is one answer; digit-by-digit pruning
from both ends, as used in the big binary-decimal searches, is another.

## 3. Toward a proof

### Why more searching cannot settle it

Treat the decimal digits of a candidate as random. A candidate with L hex
digits has about 1.204 L decimal digits, and a random D-digit number is a
palindrome with probability about 10^(-D/2). So the probability is about
10^(-0.602 L), which is 2^(-2L). There are about 2^L candidates per hex length,
so the expected number of solutions at length L is about 2^(-L), and the
expected number at all lengths beyond 38 is about 2^-38. The expected total over
all lengths from 2 upward is about one half, which is consistent with finding
none. This is a heuristic, not a theorem; the digits are not random. But it
means an extended search is overwhelmingly likely to report zero again, and
only a proof can close the question.

### What is known for related problems

- Goins (2009) proved finiteness for a related but different question: a
  d-digit decimal palindrome that is also a d-digit palindrome in another base.
  The argument leans on the two representations having the same length. Here the
  binary representation is about 3.32 times longer than the decimal one, so the
  technique does not transfer directly.
- Palindromes in bases 2 and 10 alone are conjectured to be infinite, with
  dozens known (OEIS A007632) and record holders found by pruned searches. No
  finiteness or infinitude proof exists even for two bases. Three bases is
  strictly harder in the sense that any infinitude proof would imply one for
  two bases.
- Bérczes and Ziegler (2014) study simultaneous palindromes with analytic tools
  and give explicit lists for small bounds.

### Possible lines of attack

1. **Modular obstructions.** A decimal palindrome with an even digit count is
   divisible by 11. A binary palindrome with an even bit count is divisible by
   3. A hex palindrome with an even digit count is divisible by 17. The leading
   digit structure (digits 0, 6, 9, F for the 9/F family) pins residues modulo
   small numbers. These rule out subfamilies, and a complete case analysis might
   kill all even-length or all odd-length cases, which would halve the search
   and point toward structure. A full non-existence proof this way seems
   unlikely because the constraints are finite and the families are infinite,
   but partial results are publishable.
2. **Carry analysis.** The binary structure forces the top and bottom bits to
   mirror. Decimal palindromicity forces the top and bottom decimal digits to
   mirror. Since the bottom decimal digits depend on the value mod 5^k and the
   top ones on the magnitude, one could ask whether the mirrored bit pattern
   forces a contradiction between the two after enough digits. This is the
   proof-shaped version of the meet-in-the-middle observation.
3. **Density arguments** show at most that solutions are sparse, which the
   heuristic already suggests. They cannot prove absence.

A contribution here would be a lemma that eliminates one of the four
leading-digit families for all lengths, with a proof short enough to check by
hand or in a proof assistant.
