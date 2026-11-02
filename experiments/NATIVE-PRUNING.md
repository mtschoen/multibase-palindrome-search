# Native exact pruning experiment

This standalone CPU prototype ports `exact_pruning.py` without changing its
digit alphabets, endpoint order, outside-in traversal, leaf-before-prune decision,
or exact common-prefix rejection. It supports inclusive hex lengths 1 through
64. It does not change the original enumerators. Completed search bounds are
recorded in [RESULTS.md](../docs/RESULTS.md); the program alone is not a proof
about an unexecuted range.

```powershell
clang -O3 -Wall -Wextra --rtlib=compiler-rt -o bin/exact_pruning_native.exe experiments/exact_pruning_native.c
bin/exact_pruning_native.exe 32 32
bin/exact_pruning_native.exe 64 64 --leading-bits 1 2
python -B -m unittest experiments.test_exact_pruning experiments.test_exact_pruning_native
```

The optional selector deduplicates and sorts groups. Invalid arguments exit 2.
Each flushed JSON line describes one completed group, including all reference
accounting fields and sorted integer hits. Timing is measured externally. There
is no internal time budget: a supervisor must impose a cap for expensive runs.
An interrupted group emits no completion row; earlier flushed rows remain valid.
Never sum assignments over all lengths into a 64-bit accumulator.

## Exactness where C differs

Four little-endian `uint64_t` limbs hold up to 256 bits. Every nibble occupies one
limb without straddling its boundary. Mirrored placements use OR, so an odd center
is assigned once. Assigned and remaining nibble positions are disjoint; thus
`value OR remaining[depth]` equals the Python interval's exact upper endpoint.
The maximum inner digit belongs to each alphabet, making that endpoint attainable.
All completions share the low `4 * depth` bits until the leaf decision.

Decimal conversion repeatedly divides by 10^19, then emits the highest chunk
without padding and the remaining chunks with exactly 19 digits. Five chunk slots
and a 79-byte output hold every value through 2^256 - 1 without truncated chunks.
The x86-64 `divq` helper follows the reviewed production limb-division pattern;
each high numerator is a remainder strictly smaller than the divisor, so its
quotient fits in one limb. The fallback uses unsigned 128-bit division. The
prototype includes no production C source and uses no floating-point pruning.

If the decimal endpoints have equal length D, let k be their common prefix length
capped at `min(4 * depth, D / 2)`. Since D <= 78, k <= 39. The reversed prefix is
accumulated as `sum(digit[i] * 10^i)` modulo 2^maximum_digits, then masked to k
bits. Reduction before the final mask preserves the residue modulo 2^k.
Intermediate products fit in 43 bits. A mismatch with limb zero rejects exactly
the same necessary palindrome suffix condition as Python; leaves receive a full
decimal palindrome check. Ascending outside-in digits make hits numerically sorted.

Only subtree counts at depths >= 1 are computed. The largest subtree count is
4^31 = 2^62 and the largest group contains 2^63 assignments. Disjoint rejected
subtrees plus examined leaves fit in `uint64_t`, as does the full tree's node
count (less than 2^64). Each completed group checks coverage equals assignments.

## Verification on Windows, 2026-11-02

The first new test was observed failing before implementation. The final command
above now passes 11 tests: six Python tests and five native tests. The original
port was checked against the five-test common-prefix baseline before the
optional bucket experiment was integrated.
The native tests compare every accounting/node/leaf field and sorted hits for all
four groups at lengths 1..12, 16, 20, 24, 28, 32, and groups 1/2 at length 64.
They also check CLI selection, ten invalid invocations, and 28 independent Python
bigint decimal witnesses including 2^256 - 1, limb boundaries, and decimal powers.
Only the test translation unit includes this experimental C file with its main
renamed to reach conversion directly; the shipped prototype has no test mode.
Native subprocess hang guards default to 180 seconds and can be overridden with
`NATIVE_PRUNING_TIMEOUT_SECONDS`. No test asserts real execution time.

`bash build.sh check` passed the existing CPU checks: both decimal self-tests,
740 matching binary/hex pairs, and 44,755,620 candidates through 24 hex digits
with zero residue mismatches. No GPU was used. The requested compile flags
produced zero warnings; `clang --analyze -Wall -Wextra -Xanalyzer
-analyzer-output=text experiments/exact_pruning_native.c` produced zero findings.

Best-effort LLVM coverage of the native CLI (lengths 1..32, length 64 groups 1/2,
selection and all ten invalid invocations) measured 146 lines, three missed:
97.95% line coverage, 96.12% region coverage, 93.59% branch coverage, with all
nine functions executed.
The missed lines are the internal accounting-failure block. Some CLI branches
and allocation-failure branches were not covered in this instrumented run;
CLI validation is also covered by the normal tests. The non-x86
fallback was not built here. This is not full repository or GPU coverage.

Coverage reproduction: compile with the same flags plus
`-fprofile-instr-generate -fcoverage-mapping`, run those three selections with
distinct `LLVM_PROFILE_FILE` paths under ignored `bin/`, merge with
`llvm-profdata merge -sparse`, then run `llvm-cov report` with the merged
`-instr-profile` argument quoted in PowerShell. Raw evidence remains ignored.
Test consolidation found five distinct behaviors with parameterized cases;
delta: five test methods, 109 input cases, 114 test-file lines added.

## Bounded benchmark

Each invocation below ran sequentially in an owned background Python supervisor
using `subprocess.run(..., timeout=180)` and external `perf_counter` process wall
time. All completed with exit 0 and no hits. Times include process startup and
JSON serialization and reflect concurrent machine load; they are single samples.

| Hex length | Native seconds | Python seconds |
| --- | ---: | ---: |
| 32 | 0.015630 | 0.085723 |
| 40 | 0.018241 | 0.090318 |
| 48 | 1.587001 | 4.674715 |
| 56 | 31.812243 | not run |

Commands were `bin/exact_pruning_native.exe L L` and
`python -B experiments/exact_pruning.py L L`, for each table length L. All four
groups ran each time. All non-timing fields agree at 32, 40 and 48. Length 56
native group 3 visited 3,180,646 nodes and examined 1,301,944 leaves; group 4
visited 59,041,002 nodes and examined 25,568,796 leaves. Each covered exactly
36,028,797,018,963,968 assignments. These were pre-audit benchmarks. The later
reviewed, capped 1..64 run and its completed bounds are in RESULTS.md.

The README and build instructions remain accurate for the production programs;
the experimental commands and limits are documented only here as requested.
