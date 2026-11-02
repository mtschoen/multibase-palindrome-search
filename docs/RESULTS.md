# Results

All runs used one AMD Ryzen 9 7950X3D machine (16 cores, 32 threads), Windows 11,
and an RTX 4090 where applicable. Original CPU enumeration used clang 22.1.0
with `-O3 -march=native`; the follow-up also used Python and nvcc. Enumeration
times are from each program's `done:` line; pruning timing conventions are
stated separately. Only the trivial hits 1, 3, 5, 7 and 9 were found.

## 2026-11-02: Astra-led audit and exact pruning

The complete bound is now **every integer below 2^255**, about 5.8e76.
All families completed through 63 hex digits, and the leading-1, 3, 5 and 7
families completed at length 64. Those are all possible binary/hex families
below 2^255. The native run was stopped at its agreed 18-minute process limit
during length 64, leading width 4 (digits 9/F). That group has no completion
record and is excluded; the full 2^256 range is not claimed.

| Follow-up run | Completed range | Covered assignments or enumerated candidates | Nontrivial hits | Time |
|---|---|---:|---:|---|
| Corrected GPU | all groups, 1..46 hex digits | 187,650,018,028,196 | 0 | 2,601.4 s |
| Python common-prefix pruning | all groups, 1..56 | 192,153,585,174,882,980 | 0 | 434.205 s completed-group traversal |
| Native common-prefix pruning | all groups, 1..63 | 30,744,573,469,067,487,908 | 0 | within the 1,080 s budget, including further length-64 work |
| Native common-prefix pruning | length 64, widths 1/2/3 | 9,223,372,041,149,743,104 | 0 | same run |
| Fresh original CPU enumeration | widths 1/2, 1..64 | 17,179,869,180 | 0 | 10.5 s |

Pruning coverage includes whole rejected subtrees, not individual candidate
checks. Through length 63 the native search visited 1,548,728,144 nodes and
examined 656,265,228 leaves. Every completed row matches the independent
closed-form assignment count. Every accounting/node/leaf/hit field agrees in
all 226 mutually completed Python/native groups, including all groups through
56. Python stopped partway through length 57, width 3 at its 120-second group
budget; that partial work is excluded.

The repaired GPU run completed all 184 groups with only hits 1, 3, 5, 7 and 9.
It returned 99,418,342 survivors: 44,324 short numbers, 314,763 with mirrored
nine-digit ends, and 99,059,255 rounding fall-throughs. All 32 historical
group/candidate/survivor tuples at lengths 39..46 match exactly. Length 46
again required 14 buffer splits and returned 62,754,676 survivors. The fresh
run therefore confirms the original published bound: the counter defect did
not change those recorded normal-search results.

The follow-up used Python 3.13, clang, and nvcc 13.2 on the same machine.
Measurements are single observations under concurrent load, not timing guarantees.

```powershell
.\build_cuda.bat check
bin/tripal_cuda.exe 1 46
python -B -u experiments/exact_pruning.py 1 64 --budget-seconds 120
clang -O3 -Wall -Wextra --rtlib=compiler-rt -o bin/exact_pruning_native.exe experiments/exact_pruning_native.c
bin/exact_pruning_native.exe 1 64
```

The last command was supervised with a 1,080-second process limit. The
supervisor stopped it and returned 124; only its flushed completion rows count.
To reproduce just the complete all-family range, use `1 63`; the additional
completed groups are `64 64 --leading-bits 1 2 3`. Raw output remains ignored
under `bin/`. See [the Astra audit narrative](AUDIT-2026-11-02.md) and
[the exactness argument](VERIFICATION.md).

## Original enumeration coverage

| Program | Range | Candidates tested | Non-trivial hits | Wall time |
|---|---|---|---|---|
| tripal | every integer below 2^32 | 4,294,967,295 | 0 | 0.2 s |
| tripal | every integer below 2^40 | 1,099,511,627,775 | 0 | 37.9 s |
| tripal_hex | hex palindromes below 2^64 | 8,589,934,590 | 0 | 1.6 s |
| tripal128 | base 2 + 16 palindromes, 1 to 32 hex digits | 11,453,508,260 | 0 | 23.3 s |
| tripal_wide | 1 to 32 hex digits | 11,453,508,260 | 0 | 14.8 s |
| tripal_wide | 33 to 36 hex digits | 171,799,478,272 | 0 | 229.5 s |
| tripal_wide | 37 to 38 hex digits | 549,756,862,464 | 0 | 688.8 s |
| tripal_wide | 39 to 64 hex digits, leading digits 1 and 3 only | 17,177,772,032 | 0 | 11.1 s |
| tripal_cuda (RTX 4090) | 1 to 32 hex digits | 11,453,508,260 | 0 | 0.2 s |
| tripal_cuda (RTX 4090) | 39 to 45 hex digits | 116,548,705,976,320 | 0 | 1,599.6 s |
| tripal_cuda (RTX 4090) | 46 hex digits | 70,368,752,566,272 | 0 | 960.4 s |

These original runs covered every integer below 2^184 (46 hex digits, about
2.5e55), plus the leading-digit 1 and 3 subsets to 2^256. The newer exact-pruning
bound is stated above.

The GPU run for 39 to 45 was one invocation of `bin/tripal_cuda 39 46`; it
completed every group through hex length 45 and then stopped at the first
length-46 group because a launch chunk overflowed the survivor buffer, which the
program treated as a fatal error rather than dropping candidates. The per-group
lines in its log give the counts above. Length 46 was then run separately with
the chunk-splitting fallback, which split one dense pocket of fall-through
candidates 14 times before every piece fit; the host verified 62,754,676
survivors for that length, of which 105,145 had genuinely mirrored nine-digit
ends and none was a full palindrome.

## Candidate counts by group

The orbit construction yields these candidate counts per (hex length L,
leading bits b). They are useful for checking an independent implementation.

| L | b = 1 (digit 1) | b = 2 (digit 3) | b = 3 (digits 5, 7) | b = 4 (digits 9, F) |
|---|---|---|---|---|
| 33 | 65,536 | 65,536 | 8,589,934,592 | 8,589,934,592 |
| 34 | 65,536 | 65,536 | 8,589,934,592 | 8,589,934,592 |
| 35 | 131,072 | 131,072 | 34,359,738,368 | 34,359,738,368 |
| 36 | 131,072 | 131,072 | 34,359,738,368 | 34,359,738,368 |
| 37 | 262,144 | 262,144 | 137,438,953,472 | 137,438,953,472 |
| 38 | 262,144 | 262,144 | 137,438,953,472 | 137,438,953,472 |

Pattern: for b = 3 and b = 4 the count is 2^L when L is odd and 2^(L-1) when L
is even. The b = 1 and b = 2 groups grow by a factor of 2 every two hex digits,
which is why Regan could reach 64 hex digits for them in 2010 and why the same
run takes 11 s here.

Sanity checks on totals: the hex palindrome count below 2^64 is exactly
2 * (16^8 - 1) = 8,589,934,590, matching the closed form. Candidates visited
equals candidates planned in every completed run. Equality alone does not rule
out overlaps or gaps; the partition proof and independent list comparisons
provide the additional evidence.

## Throughput

tripal_wide processes about 8.0e8 candidates per second on this machine, about
20 ns per candidate per physical core. The 37 to 38 chunk is the reference
measurement: 549,756,862,464 candidates in 688.8 s.

tripal_cuda processes about 7e10 candidates per second on the RTX 4090, roughly
90 times the CPU program. Per hex length in the 39 to 45 run, both large groups
together: 39 in 15.2 s, 41 in 60.5 s, 43 in 239.7 s, 45 in 969.4 s, 46 in 960.4 s.

Survivor counts per group in the GPU run vary by two orders of magnitude (3,331
to 7,446,010) because fall-through candidates share leading-bit prefixes and are
passed to the host in blocks; this is conservative and does not affect
correctness, but it is why the survivor buffer needs the chunk-splitting
fallback.

## Commands

```
bin/tripal                          # every integer below 2^32
bin/tripal 1099511627776            # every integer below 2^40
bin/tripal_hex 16                   # hex palindromes up to 16 hex digits
bin/tripal128 32                    # orbit construction, 1 to 32 hex digits
bin/tripal_wide 1 32
bin/tripal_wide 33 36
bin/tripal_wide 37 38
bin/tripal_wide 39 64 32 12         # 32 threads, leading bits 1 and 2 only
```

## Projection for exhaustive enumeration

For the original enumerators, each additional hex digit for the 5, 7, 9, F
subsets doubles the work on average. At the measured rates on this machine:

| Hex length | Candidates | 16-core CPU | RTX 4090 |
|---|---|---|---|
| 47 and 48 | 2^48 | 4 days | 70 min |
| 49 and 50 | 2^50 | 16 days | 4.5 h |
| 51 and 52 | 2^52 | 2 months | 18 h |
| 55 and 56 | 2^56 | 3 years | 12 days |
| 63 and 64 | 2^64 | about 700 years | about 8 years |
