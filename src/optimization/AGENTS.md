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
