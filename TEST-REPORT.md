# multibase-palindrome-search - Test Report

2026-11-02T10:14:00Z

| Field | Value |
|---|---|
| Status | PASS for executed checks; coverage limits below |
| Mode | best-effort |
| Tests | 11 unittest tests passed, 0 failed; 3 GPU regression cases passed; 2 CPU decimal self-tests passed; 740 pair-list entries agreed; 44,755,620 direct verify candidates had 0 residue/prefilter mismatches |
| Git | [Native implementation baseline db34404](https://github.com/mtschoen/multibase-palindrome-search/commit/db344047c9e7c7e08ca36bcbc56bc84fa1f67139), local branch `fix/gpu-survivor-counter`, plus native-test metadata compatibility assertions |
| Coverage | Python: 131/131 statements and 60/60 branches; native C: 143/146 lines and 73/78 branches in the instrumented CLI run; 0 exclusions; original C/CUDA source coverage unmeasured |
| Lint | Ruff: 0 findings; native clang `-Wall -Wextra`: 0 warnings; clang static analyzer: 0 findings; 0 added suppressions |
| GPU memory check | Compute Sanitizer: 0 errors in the focused regression executable |

## Commands

```powershell
bash build.sh check
.\build_cuda.bat check
compute-sanitizer --tool memcheck --error-exitcode 1 bin/tripal_cuda_test.exe
python -B -m unittest experiments.test_exact_pruning experiments.test_exact_pruning_native -v
$env:COVERAGE_FILE = 'bin/pruning.coverage'
python -B -m coverage run --branch --source=experiments.exact_pruning -m unittest experiments.test_exact_pruning -v
python -B -m coverage report -m
python -m ruff check --no-cache experiments
clang --analyze -Wall -Wextra -Xanalyzer -analyzer-output=text experiments/exact_pruning_native.c
clang -O3 -Wall -Wextra --rtlib=compiler-rt -fprofile-instr-generate -fcoverage-mapping -o bin/exact_pruning_native_coverage.exe experiments/exact_pruning_native.c
$env:LLVM_PROFILE_FILE = 'bin/parent-native-%p.profraw'
bin/exact_pruning_native_coverage.exe 1 32
bin/exact_pruning_native_coverage.exe 64 64 --leading-bits 1 2
bin/exact_pruning_native_coverage.exe 1 2 --leading-bits 4 3 2 1 4
# The ten invalid invocations in test_invalid_cli also ran, each requiring exit 2.
$profilePaths = Get-ChildItem bin/parent-native-*.profraw | ForEach-Object FullName
llvm-profdata merge -sparse @profilePaths -o bin/parent-native.profdata
llvm-cov report bin/exact_pruning_native_coverage.exe -instr-profile=bin/parent-native.profdata
git diff --check
```

## Coverage limits

The three unexecuted native lines are the internal accounting-failure block.
Allocation-failure and some guard branches remain unexecuted. The non-x86
division fallback was not built on this host. No exclusions were added.
