# Results

All runs on one machine: AMD Ryzen 9 7950X3D (16 cores, 32 threads), Windows 11,
clang 22.1.0, built with `-O3 -march=native`. Wall times are from the program's
own `done:` line. Hits are the trivial single digits 1, 3, 5, 7 and 9 wherever
the range includes them; no other hit occurred in any run.

## Coverage

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

Combined: every integer below 2^152 is covered, and the leading-digit 1 and 3
subsets are covered to 2^256.

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
equals candidates planned in every run, which rules out gaps in the thread
partition.

## Throughput

tripal_wide processes about 8.0e8 candidates per second on this machine, about
20 ns per candidate per physical core. The 37 to 38 chunk is the reference
measurement: 549,756,862,464 candidates in 688.8 s.

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

## Projection

Each additional hex digit for the 5, 7, 9, F subsets doubles the work. At the
measured rate on this machine:

| Hex length | Candidates | Estimated wall time |
|---|---|---|
| 39 | 2^40 | 23 min |
| 40 | 2^40 | 23 min |
| 41 and 42 | 2^42 | 1.5 h |
| 43 and 44 | 2^44 | 6 h |
| 47 and 48 | 2^48 | 4 days |
| 63 and 64 | 2^64 | about 700 years |
