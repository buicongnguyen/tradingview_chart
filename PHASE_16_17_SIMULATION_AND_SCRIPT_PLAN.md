# Phase 16–17: Interactive Simulation and Safe Script Import

## Objective

Add a deterministic local trading simulator and a non-executing Pine-style
strategy importer without introducing a broker connection, TradingView market
data, an untrusted-code runtime, or a copyleft runtime dependency.

The user supplies market history through the application's existing providers
or CSV loader. A supported script is compiled into the same native
`StrategyDefinition` used by Backtest, Replay, Theory Validation, Scanner, and
Alerts.

## Scope decisions

- Keep the application long-only and limited to one open position.
- Evaluate rules only after a completed candle.
- Execute accepted market orders at the following candle's open.
- Apply configured slippage and fixed per-side commission to every fill.
- Use the current chart's validated, completed OHLCV history.
- Use completed higher-timeframe bars through the existing
  `StrategyEvaluator` alignment rules.
- Treat an open position at the end of available history as marked to market;
  do not invent a same-close forced execution.
- Never execute, transpile, or evaluate arbitrary pasted source code.
- Store pasted source only in local application settings. Do not add imported
  user scripts to the repository or upload them.
- Clearly identify the importer as a supported subset, not full Pine Script
  compatibility and not an official TradingView integration.

## Phase 16: Interactive Trading Simulator

### Core engine

Create `strategy/trading_simulation.hpp/.cpp` with:

- `SimulationMode`: Automatic, Manual, Assisted.
- `SimulationAction`: Enter, Exit.
- `SimulationOrderState`: None, Proposed, Pending.
- `SimulationConfig`: strategy, execution assumptions, mode, start index.
- `SimulationAccount`: cash, quantity, entry price, equity, realized and
  unrealized P/L, high-water equity, drawdown, and pending/proposed action.
- `SimulationDecision`: signal time, action, origin, disposition, and detail.
- `TradingSimulationSession`: reset, step, request manual action,
  approve/reject proposal, and clear.

### Event sequence

For each step from candle N to N+1:

1. Execute an already accepted order at candle N+1 open.
2. Apply slippage and commission.
3. Update position excursion and account equity using candle N+1.
4. Evaluate the frozen strategy at candle N+1 close.
5. In Automatic mode, queue the matching action.
6. In Assisted mode, expose it as a proposal.
7. In Manual mode, report the rule state but do not queue it.

The initial visible candle is evaluated at reset so a matching signal can be
acted on at the first subsequent open.

### UI

Add a `Simulation` tab to Strategy Lab:

- Start moment selector, Latest/Replay-moment helpers, and mode selector.
- Initial capital, allocation, commission, slippage, and fractional-share
  assumptions shared with Backtest.
- Start/Reset, Step, Play/Pause, Buy, Sell, Approve, Reject, and Stop actions.
- Account summary with current time, cash, position, equity, realized and
  unrealized P/L, total return, drawdown, and rule/order state.
- Trade table and decision audit table.
- Chart replay synchronized to the simulator's revealed bars.
- Local auto-resume snapshot guarded by symbol, timeframe, source fingerprint,
  and strategy fingerprint.

### Phase 16 tests

- Signal at candle N executes only at candle N+1 open.
- Appending future bars does not change state through an earlier moment.
- Costs, fractional/integer sizing, insufficient cash, and final open position
  reconcile correctly.
- Automatic, Manual, and Assisted modes produce distinct expected behavior.
- Proposals expire rather than execute if the user advances without approval.
- Invalid sources/configuration and incompatible snapshots are rejected.
- UI smoke test starts and steps a simulation.

## Phase 17: Safe Pine-style Strategy Import

### Parser and compiler

Create `strategy/pine_strategy_importer.hpp/.cpp`. The parser operates on text
only and produces diagnostics; it never evaluates arbitrary code.

Initial supported subset:

- `//@version=5` and `//@version=6`.
- One `strategy(...)` declaration.
- `input.int()` and `input.float()` numeric defaults.
- OHLCV operands.
- `ta.sma(close, period)`, `ta.ema(close, period)`, and
  `ta.rsi(close, period)`.
- `ta.crossover()`, `ta.crossunder()`, `>`, and `<`.
- Flat `and` or flat `or` condition expressions.
- Named condition variables.
- `if condition` followed by `strategy.entry(..., strategy.long)` or
  `strategy.close(...)`.
- `when = condition` on supported entry/close calls.
- `initial_capital` and percentage-of-equity allocation where representable by
  the native execution model.

Explicitly rejected with line diagnostics:

- Short positions, pyramiding, multiple positions, and reversals.
- Stop, limit, stop-limit, trailing, or partial orders.
- Tick-level execution, order-fill recalculation, and bar magnifier behavior.
- `request.*`, imports, libraries, user-defined functions, loops, arrays,
  matrices, maps, objects, drawing objects, and network/file access.
- History offsets, future lookahead, unsupported sources, and mixed nested
  boolean expressions.
- More than 1,000 lines, 64 KiB of source, 128 symbols, or 16 conditions per
  entry/exit group.

### Script Lab UI

Add a `Script` tab:

- Source editor with a documented example.
- Compile and Apply buttons.
- Diagnostics with severity and line number.
- A generated native-rule preview.
- Execution-assumption preview.
- A notice that source is local, subset-only, and must be user-authored or
  appropriately licensed.

Applying a successfully compiled script updates the visual rule editor and
supported Backtest assumptions. The user can then save the result as an
ordinary named native strategy.

### Phase 17 tests

- Compile a v6 EMA-cross strategy into the expected native definition.
- Resolve numeric inputs and flat all/any expressions.
- Map supported capital and allocation properties.
- Reject shorting, unsupported calls, nested/mixed boolean logic, oversized
  input, invalid periods, missing entry/exit, and deceptive lookahead syntax.
- Verify diagnostics identify the source line.
- Verify compilation is deterministic and cannot mutate application state.
- UI smoke test compiles and applies the built-in example.

## Logic review checklist

- Account identity: `cash + quantity * current close == equity`.
- Closed-trade identity: cash after exit reconciles with both commissions and
  both slippage-adjusted fills.
- No action generated from candle N can execute before N+1.
- No source values after the current simulated candle affect a decision.
- Higher-timeframe values are available only after their candle completes.
- Manual/assisted commands are validated against current position state.
- Simulator never silently force-closes a final open position.
- Importer rejects unsupported semantics instead of approximating them.
- Imported source is data, never executable Qt/JavaScript/C++.
- Existing native strategies and schema remain backward compatible.

## Documentation, packaging, and deployment

- Update README and About text with Simulation and Script Lab behavior,
  limitations, data provenance, and licensing warnings.
- Add new core sources to CMake and native tests.
- Extend the hidden UI smoke test and Windows package smoke test.
- Run all native CTest targets, web renderer tests, `git diff --check`, and
  packaged Windows smoke validation.
- Commit the complete reviewed scope on the current feature branch.
- Push through the existing SSH remote.
- Open and merge a reviewed pull request into `main`.
- Verify Windows and Android GitHub Actions on `main`.
- Publish a GitHub release with the tested Windows package; attach the Android
  artifact when the CI build is complete. GitHub Pages is not used because this
  deliverable is a native Qt application, not a static website.
