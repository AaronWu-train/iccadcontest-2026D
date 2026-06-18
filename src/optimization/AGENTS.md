# Optimization directory

Follow root [`AGENTS.md`](../../AGENTS.md): **DebugProgress** and **Optimization module**.

Architecture details:

- [`docs/optimization-architecture.md`](../../docs/optimization-architecture.md)
- [`docs/optimization-algorithms.md`](../../docs/optimization-algorithms.md)
- [`docs/optimization-complexity.md`](../../docs/optimization-complexity.md)
- [`docs/optimization-experiment-parameters.md`](../../docs/optimization-experiment-parameters.md)

Local rules:

- Optimizer hot paths should use `NodeId`, `EdgeId`, and reversible `ClockTreeEdit`.
- Do not remove original contest nodes; only inserted buffers are removable.
- Keep `TimingState` focused on timing metrics, score cache, apply/undo, and snapshot/restore.
- Keep candidate generation, greedy policy, SA acceptance, and restart strategy inside optimizers or SA-family helpers.
- Optimizer telemetry must use `DebugProgress`, not direct `std::cerr`.
- A1-A13 aliases live in `factory.cpp`; A1-A9 numeric aliases must keep their existing behavior.
- A1-A5 live in `greedy/` and share only `GreedyOptimizer`; their intended difference is the
  `CandidatePolicy` branch inside that class.
- A6/A10 live in `two_step/`; keep their TwoStepSlackThenScore AcceptPolicy local.
- A7/A8/A11/A12 live in `sa/`; keep Metropolis and iterated-SA restart logic local.
- A9/A13 live in `tabu/`; keep tabu memory, aspiration, and accept/reject policy local.
- `candidate_policy.*` is the only shared CandidatePolicy action-generation/apply/undo layer.
- Do not reintroduce `search_utils.*`, `greedy_search.*`, or variant wrapper files for the main
  optimizers.
- Progress TSV and visual frames are optional via `OptimizerContext` writers and must remain
  lightweight. Do not record every candidate trial.
