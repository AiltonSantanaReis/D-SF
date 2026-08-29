# Aion Research Lab R2.1 — Release Verification

Release source version: **0.0.2**

## Clean compiler verification
The release source tree was configured and built independently with:

- GCC 14.2, Release, x86-64 Linux: no compiler warnings from the configured strict kernel warning set; 4/4 CTest tests passed.
- Clang 17, Release, x86-64 Linux: no compiler warnings from the configured strict kernel warning set; 4/4 CTest tests passed.

Both compiler builds reproduced the R2.1 Execution Kernel verification hash:

`657f7bd1092e03c74acf7a38b7a70243f3a8decef268fcce0c552b4195f34a94`

## Sanitizer verification
Dedicated R2.1 patch and execution tests passed under AddressSanitizer + UndefinedBehaviorSanitizer without a reported error.

The R2.1 concurrent Execution Kernel test passed under ThreadSanitizer without a reported data race. A smaller persistent parallel-publisher workload also passed under ThreadSanitizer. The full patch suite under ThreadSanitizer is not claimed because its intentionally thread-heavy reference path exceeded the sandbox single-invocation runtime limit.

## Scope
These results verify the supplied CPU reference source in the named environment. They do not constitute GPU, Windows, ARM or universal deterministic-performance evidence.
