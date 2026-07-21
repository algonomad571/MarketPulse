# Hardened Mathematical Regression Framework (M4.1)

To guarantee that any future performance optimizations (e.g., SIMD vectorization, cache blocking, asynchronous ring buffers) do not silently corrupt the quantitative output of the MarketPulse Intelligence Engine (MPIE), a deterministic, zero-dependency mathematical regression suite has been introduced.

## Architecture

The framework has been modularized for long-term maintainability:
- **`test_framework.hpp`**: Contains the assertion engine (`ASSERT_MATH_EQUAL`), tracking global/feature-specific test counts, and printing detailed diagnostics (Expected vs Actual, Absolute/Relative Error).
- **`golden_dataset.hpp`**: Acts as the single source of truth, pre-generating ~30 highly specific handcrafted market sequences spanning edge cases (zero volumes, crossed markets, huge/tiny prices).
- **`test_price_features.cpp`**: Dedicated, targeted test suites for `Spread`, `MidPrice`, `MicroPrice`, `WAP`, and `LogReturn`.
- **`test_edge_cases.cpp`**: Asserts the structural integrity of the `FeatureValidator` against corrupted floating-point injections (NaN, Infinity).
- **`test_pipeline_integration.cpp`**: Pumps the entire `golden_dataset` through the `StateManager -> FeaturePipeline -> FeatureValidator` sequential stack, verifying the completely serialized `FeatureVector`.

## Testing Philosophy

- **Floating-Point Tolerance**: `constexpr double EPS = 1e-9;`. All floating point arithmetic comparisons are guarded by `std::abs(actual - expected) < EPS`. `==` is strictly prohibited.
- **Diagnostics**: Silent fails are unacceptable. The runner immediately outputs Absolute and Relative errors to help trace numerical drifts.
- **CI/CD Ready**: The executable returns `EXIT_SUCCESS` strictly on a 100% pass rate, triggering `EXIT_FAILURE` if a single assertion drifts, enforcing strict gating for merges.

## Edge Cases Explicitly Verified
- Zero spread (locked/crossed markets)
- Zero bid/ask sizes (phantom volume)
- Zero total volume (division by zero safeguards)
- Missing historical ticks (boundary safety)
- Negative/Zero price histories (logarithm safety)
- Extremely small/large boundary pricing
- Invalid state injections (NaN / Inf)

## Regression Extension
When implementing future passes (Pass 2 Book Imbalance, Pass 3 OFI, Pass 4 Statistical VWAP), you must:
1. Hardcode expected variables into `ExpectedOutputs` inside `golden_dataset.hpp`.
2. Compute the exact theoretical output for all 30 handcrafted ticks.
3. Write a dedicated `test_[feature].cpp` and `ASSERT_MATH_EQUAL` it.
4. Update the coverage report inside `main_test.cpp`.
