# Project guidance

- Read README.md, docs/RESULTS.md and docs/VERIFICATION.md before claiming a bound.
- Astra's audit is narrated in docs/AUDIT-2026-11-02.md.
- Run `sh build.sh check` for CPU checks and `.\build_cuda.bat check` from
  PowerShell for GPU regressions. Coordinate exclusive GPU use.
- Run `python -B -m unittest experiments.test_exact_pruning experiments.test_exact_pruning_native`
  for exact-search tests. Native tests rebuild the executable; do not run them
  while that executable is searching.
- Python coverage: `python -B -m coverage run --branch --source=experiments.exact_pruning -m unittest experiments.test_exact_pruning`.
  Put `COVERAGE_FILE` under ignored `bin/`. TEST-REPORT.md records the native
  LLVM coverage commands and limits as well.
- Budget-expired or killed groups are incomplete. Count only explicit completion
  records. Sum group counts with arbitrary-precision integers: their aggregate
  can exceed 64 bits even though each supported group fits.
- Keep raw logs, JSONL, profiles and binaries under ignored `bin/`; publish counts
  and reproduction commands. Preserve the rejection proof and independent
  reference comparisons when changing a pruner.
