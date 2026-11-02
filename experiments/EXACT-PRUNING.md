# Exact CPU prefix/suffix pruning experiment

This standalone Python prototype branches on mirrored hexadecimal digits from
the outside in. It does not change the production search. All arithmetic used
for pruning is exact integer arithmetic; decimal strings extract digits exactly.
There are no runtime dependencies outside the Python standard library.

## Why rejection is conservative

For hexadecimal length `length`, independent digit position `position` has weight
`16**position + 16**(length-1-position)`. The odd center uses just one power.
The four endpoint/inner-digit groups are respectively `{1}/{0,1}`,
`{3}/{0,3}`, `{5,7}/{0,2,5,7}`, and `{9,F}/{0,6,9,F}`.
With `half_length = ceil(length/2)`, their complete assignment counts are
`2**(half_length-1)` for each of the first two groups and
`2**(2*half_length-1)` for each of the last two.

After an endpoint has been selected, all remaining weights are positive and
every remaining digit can be zero or the group's maximum digit. Consequently
the partial value is the exact minimum completion; adding the maximum digit
times the sum of the remaining weights gives the exact maximum completion.
Fixing `depth` outside digits fixes the bottom `4*depth` bits. The shared prefix
of the two decimal bounds applies to every intervening integer only when both
bounds have the same decimal digit count. A boundary crossing disables pruning.

Let `count` be the minimum of the common prefix length, fixed low bit count,
and half the decimal digit count rounded down. Any decimal palindrome in the
interval must end in the reverse of that `count`-digit prefix, modulo
`10**count`. It must therefore have that suffix's residue modulo `2**count`.
If the fixed low bits disagree, every completion in that subtree is impossible.
Otherwise search continues; all surviving leaves receive a full decimal reversal
check. Leading zeroes in a reversed suffix are valid and do not change its
integer residue. No floating point arithmetic or logarithmic estimate is used.

Pruned subtrees are disjoint. Each contributes the full remaining digit-product
cardinality; each examined leaf contributes one. A completed search asserts
that their sum equals the closed-form count. This accounting alone does not
establish correctness: independent exhaustive comparisons below check the
actual candidate sets, bounds, residues, and hits.

## Reproduction and tests

Run from the worktree root:

```powershell
python -B -m unittest experiments.test_exact_pruning -v
$env:COVERAGE_FILE = 'bin/pruning.coverage'
python -B -m coverage run --source=experiments.exact_pruning -m unittest experiments.test_exact_pruning -v
python -B -m coverage report -m
$env:PYTHONPYCACHEPREFIX = Join-Path (Get-Location) 'bin/pycache'
python -m py_compile experiments/exact_pruning.py experiments/test_exact_pruning.py
python -m ruff check --no-cache experiments/exact_pruning.py experiments/test_exact_pruning.py
```

Coverage.py and Ruff were already installed; neither is needed to run the search
or unittest. On 2026-11-02, five test methods passed, with 96/96 search statements
covered (100%), no exclusions, zero Ruff findings, and successful compilation.
No repository coverage or Python lint configuration existed, so this is scoped
best-effort verification, not a claim about coverage of the existing C/CUDA code.
The parent owns the full production suite and repository documentation review.

The initial test run failed because the requested implementation module did not
exist. Tests now compare unrestricted mirrored hexadecimal halves, filtered by
binary string reversal, against the actual constructed sets and search hits for
every length 1 through 8 and all four groups. They verify every partial prefix
through length 6 against independently enumerated completion minima, maxima,
and fixed residues, including odd centers. Separate interval tests cover small
integer intervals and powers-of-ten boundaries through `10**29`, with differing
fixed bit counts. CLI tests cover selection, invalid arguments, trivial positive
controls, and interruption. Budget tests inject a clock and never assert on
elapsed real time. Five methods consolidate these related cases in one file.

## Benchmarks

Measured on Windows, AMD Ryzen 9 7950X3D, Python 3.13.15. Each length below was
a separate single-process run, all groups, with a 240-second per-group budget:

```powershell
foreach ($length in @(20,24,28,32)) {
    $duration = Measure-Command {
        python -B experiments/exact_pruning.py $length $length --budget-seconds 240 |
            Tee-Object -FilePath "bin/pruning-$length.log"
    }
    Write-Output "length=$length process_seconds=$($duration.TotalSeconds)"
}
```

All runs completed, covered the expected number of assignments, and found zero
hits. Traversal time is the sum of reported `elapsed_nanoseconds`, converted to
seconds; it excludes interpreter startup, imports, argument parsing, weight
construction, and JSON output. Process time includes those costs and PowerShell
pipeline handling. Timings are single measurements, not a statistical study.

| Hex length | Covered assignments | Visited nodes | Pruned branches | Exact leaf checks | Traversal seconds | Process seconds |
|---|---:|---:|---:|---:|---:|---:|
| 20 | 1,049,600 | 5,298 | 1,707 | 2,264 | 0.0037352 | 0.0549700 |
| 24 | 16,781,312 | 6,422 | 2,258 | 2,552 | 0.0050985 | 0.0563362 |
| 28 | 268,451,840 | 16,524 | 5,327 | 7,056 | 0.0137662 | 0.0662944 |
| 32 | 4,295,032,832 | 31,058 | 10,015 | 13,280 | 0.0273697 | 0.0843059 |

Per-group accounting from the same runs follows. In every row, pruned assignments
plus leaves equals the assignment count; covered assignments equals that count.
Visited nodes omit the synthetic root and include examined leaf nodes.

| Length | Leading bits | Assignments | Nodes | Pruned branches | Pruned assignments | Leaves |
|---|---:|---:|---:|---:|---:|---:|
| 20 | 1 | 512 | 17 | 9 | 512 | 0 |
| 20 | 2 | 512 | 1 | 1 | 512 | 0 |
| 20 | 3 | 524288 | 3218 | 1010 | 522884 | 1404 |
| 20 | 4 | 524288 | 2062 | 687 | 523428 | 860 |
| 24 | 1 | 2048 | 23 | 12 | 2048 | 0 |
| 24 | 2 | 2048 | 11 | 6 | 2048 | 0 |
| 24 | 3 | 8388608 | 3230 | 1043 | 8387228 | 1380 |
| 24 | 4 | 8388608 | 3158 | 1197 | 8387436 | 1172 |
| 28 | 1 | 8192 | 3 | 2 | 8192 | 0 |
| 28 | 2 | 8192 | 45 | 23 | 8192 | 0 |
| 28 | 3 | 134217728 | 5082 | 1824 | 134215740 | 1988 |
| 28 | 4 | 134217728 | 11394 | 3478 | 134212660 | 5068 |
| 32 | 1 | 32768 | 1 | 1 | 32768 | 0 |
| 32 | 2 | 32768 | 1 | 1 | 32768 | 0 |
| 32 | 3 | 2147483648 | 27850 | 8892 | 2147471652 | 11996 |
| 32 | 4 | 2147483648 | 3206 | 1121 | 2147482364 | 1284 |

For comparison, the existing primary-checkout `tripal_wide` CPU binary was run
read-only with arguments `length length 32`, using 32 threads. Its reported
visited counts agreed exactly at each length; it reported zero hits and elapsed
times of 0.0, 0.0, 0.2, and 4.2 seconds respectively. The 0.0 values are rounded,
not zero-cost runs. The primary checkout can be resolved without a machine path:

```powershell
$primaryCheckout = (git worktree list --porcelain | Select-Object -First 1).Substring(9)
foreach ($length in @(20,24,28,32)) {
    Start-Process -FilePath (Join-Path $primaryCheckout 'bin/tripal_wide') `
        -ArgumentList "$length","$length",'32' -WindowStyle Hidden -Wait `
        -RedirectStandardOutput "bin/cpu-$length.log" `
        -RedirectStandardError "bin/cpu-$length-error.log"
}
```

The length-32 CPU comparison was launched in the background and collected after
completion; no run approached 30 seconds. All raw output and coverage artifacts
remain ignored under `bin/` and are not committed.

## Limits and next use

The measured length-32 prototype process was faster than that existing CPU run;
the comparison uses different languages and thread counts. There was no GPU use
or CUDA timing comparison, and these four lengths establish no asymptotic bound.
Branch counts depend strongly on length and group. No larger search limit or
non-existence theorem follows from this experiment. Known positive controls are
only the trivial hits; no nontrivial triple-base palindrome is known here.

The range arguments are inclusive; `--leading-bits 1 4` selects those groups.
JSON lines contain exact counts, sorted hits, completion status and nanosecond
traversal time. A cooperative budget is checked every 4096 nodes, starting at
the first node. Expiry returns incomplete coverage and CLI exit status 1, stops
the requested range, and cannot certify that group. It is not a hard process
timeout. Recursion depth and Python's integer-to-string limits also constrain
very large requested lengths; only the documented small tests and benchmark
lengths were validated. Nothing is integrated into the production pipeline.
