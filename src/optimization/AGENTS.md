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
- A1-A9 aliases live in `factory.cpp`; do not keep old alias compatibility.
- A1-A5 live in `greedy/` and share only `GreedyOptimizer`; their intended difference is the
  `CandidatePolicy` branch inside that class.
- A6 lives in `two_step/`; keep its TwoStepSlackThenScore AcceptPolicy local.
- A7/A8 live in `sa/`; keep Metropolis and iterated-SA restart logic local.
- A9 lives in `tabu/`; keep tabu memory, aspiration, and accept/reject policy local.
- `candidate_policy.*` is the only shared CandidatePolicy action-generation/apply/undo layer.
- Do not reintroduce `search_utils.*`, `greedy_search.*`, or variant wrapper files for the main
  optimizers.
- Progress TSV and visual frames are optional via `OptimizerContext` writers and must remain
  lightweight. Do not record every candidate trial.
