# Annealing Optimizer Notes

This file is kept as a compatibility pointer for older references.

The optimizer architecture is now documented in:

- [`optimization-architecture.md`](optimization-architecture.md)
- [`optimization-algorithms.md`](optimization-algorithms.md)
- [`optimization-complexity.md`](optimization-complexity.md)
- [`optimization-experiment-parameters.md`](optimization-experiment-parameters.md)

The old `SkewModel` / `sa_common` split has been replaced by:

- `TimingState`: incremental timing and score cache.
- `sa_search`: shared SA-family search helpers.

For current maintenance work, update the two documents above instead of duplicating details here.
