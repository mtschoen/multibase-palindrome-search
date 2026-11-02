# Bounded prefix buckets, 2026-11-02

This optional exact Python experiment preserves `interval_rejects` and the
default `common` search. Select `--pruner common|buckets`; every JSON summary
now names its pruner. `bucket_interval_rejects` is a pure public predicate.
Production C/CUDA, build files, existing results and verification are untouched.

## Soundness and bounds

For a nonnegative ordered interval of one decimal digit length D, choose
`k <= min(fixed_bits, D//2)`. Every decimal palindrome in that interval has
a k-digit prefix P between `lower//10**(D-k)` and `upper//10**(D-k)` inclusive.
Its last k digits equal reverse(P), including suffix leading zeroes. Because
`2**k` divides `10**k`, its residue modulo `2**k` equals reverse(P) modulo
`2**k`. If every possible prefix disagrees with the fixed residue, there is
no palindrome completion. Agreement only permits further search.

First use the old common-prefix rejection as a safe fast path. For positive
width W with w decimal digits, start at `k=min(fixed_bits,D//2,D-w)`.
Then `10**(D-k)>W`, so at most two prefixes occur. Try one extra digit only
when its range contains at most four prefixes. Another extension cannot fit:
if the cap did not bind, W is at least `10**(w-1)`, spanning at least ten
prefix steps at that next scale. Zero width uses the full permitted cap.
Selection uses integer arithmetic and decimal strings, with no float estimates.

An interval crossing exactly one power of ten splits into two digit-length
pieces and rejects only if both pieces reject. Intervals spanning more digit
lengths conservatively pass. Zero and single-digit pieces cannot reject.
The API shares the old predicate's ordered, nonnegative bounds/bits contract.
At most eight prefixes are enumerated across two pieces; enumeration does not
grow with interval width. String and integer-operation costs still grow with D.

## Verification

The new boundary test first ran against the old predicate and produced three
expected assertion failures, rather than an import error: intervals 1900..2099,
9800..10020 and 9900..10099, residue zero with four fixed bits. It passed after
implementation. The fallback used for that red run was then removed.

Six methods pass, up from five. Existing tests now compare BOTH methods against
independent candidate enumeration and hits for lengths 1..8, all four groups
(64 search comparisons). All small intervals with lower 0..80 and upper through
100 are checked for conservative rejection, plus larger narrow intervals and
powers-of-ten boundaries through `10**29`. Palindrome witnesses include zero,
odd/even lengths and 1221 in 1200..1299 with fixed value 3, fixed_bits 1.
Partial search intervals and injected-clock interruption checks cover both
methods. CLI checks cover defaults, selection, invalid selection and summaries.

Commands run from this worktree, with no dependency installation:

```powershell
$env:COVERAGE_FILE = 'bin/buckets.coverage'
python -B -m coverage run --branch --source=experiments.exact_pruning -m unittest experiments.test_exact_pruning -v
python -B -m coverage report -m
python -m ruff check --no-cache experiments/exact_pruning.py experiments/test_exact_pruning.py
$env:PYTHONPYCACHEPREFIX = Join-Path (Get-Location) 'bin/pycache'
python -m py_compile experiments/exact_pruning.py experiments/test_exact_pruning.py
python -B experiments/exact_pruning.py 1 2 --pruner buckets --budget-seconds 1
python -B experiments/exact_pruning.py 1 1
```

Result: 131/131 statements and 60/60 branches covered, no exclusions or partial
branches, zero Ruff findings, compilation and CLI positive controls passed.
Coverage is scoped to this experiment. Parent owns repository-wide gates/docs.

## Runtime comparison

Actual benchmark driver: sequential `search(length, leading_bits, 30, method)`
for length in `(32,40,48)`, then method in `('common','buckets')`, then leading
bits in `range(1,5)`. It printed `json.dumps(asdict(result))` per call, launched
with `Start-Process python -ArgumentList '-B','.claude/scripts/bucket-benchmark.py'
-WindowStyle Hidden` and separate stdout/stderr files under ignored `bin/`.
The temporary driver was removed. Equivalent per-case CLI reproduction is
`python -B experiments/exact_pruning.py 48 48 --pruner buckets --budget-seconds 30`.
Each group's cooperative budget was 30 seconds; all 24 groups completed.
These are single traversal measurements, excluding process startup and setup.

| Length | Method | Covered assignments | Nodes | Leaves | Pruned branches | Seconds |
|---|---|---:|---:|---:|---:|---:|
| 32 | common | 4295032832 | 31058 | 13280 | 10015 | 0.0273552 |
| 32 | buckets | 4295032832 | 27814 | 11808 | 9054 | 0.0368868 |
| 40 | common | 1099512676352 | 39140 | 16792 | 12564 | 0.0386960 |
| 40 | buckets | 1099512676352 | 34776 | 14828 | 11255 | 0.0499730 |
| 48 | common | 281474993487872 | 4187218 | 1839076 | 1301333 | 4.3087436 |
| 48 | buckets | 281474993487872 | 3720262 | 1629876 | 1160316 | 5.7311764 |

All hits were empty; both methods covered identical assignment counts.
Buckets visited 10..11% fewer nodes but took 29..35% longer on these samples.
Keep it as an optional stronger-predicate comparison, not a performance win.
There was no GPU use, process stopping, production integration or unbounded
sweep. Host contention and length/group sensitivity limit timing conclusions.
No asymptotic claim, equidistribution theorem or new search bound follows.
Existing recursion and integer-to-string limits still apply at huge lengths.
