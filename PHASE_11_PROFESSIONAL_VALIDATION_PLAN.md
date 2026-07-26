# Phase 11 — professional validation and portfolio risk

Status: implemented for version 0.7.0; release verification is recorded in the
repository handoff.

## 1. Why this phase is next

Version 0.6 can chart, compare, research, replay, alert, backtest, and account
for a local paper portfolio. The remaining professional-risk problem is that
those calculations still consume provider bars without an explicit adjustment
policy or a visible quality report. Adding more signals before fixing that
boundary would make precise-looking results from ambiguous inputs.

The implementation order is therefore:

1. establish what each price series means and whether it is fit for analysis;
2. measure whether a strategy remains credible outside one backtest; and
3. measure portfolio risk only from compatible, sufficiently complete series.

This phase remains educational and local. It does not add broker execution,
investment advice, background trading, a TradingView data feed, or a promise
that free provider data is exchange-grade.

## 2. Review findings that shape the design

### Data integrity

- Yahoo requests already ask for dividends and splits, but version 0.6 discards
  those events and its adjusted-close array.
- Provider parsers skip unusable rows without reporting how many were rejected.
- The SQLite cache key does not include an adjustment mode. Persisting adjusted
  bars under the existing key would mix raw and transformed history.
- Twelve Data and local CSV input do not provide the same corporate-action
  fields as Yahoo, so an adjustment request cannot be silently assumed to have
  succeeded.
- Gap detection must distinguish an irregular interval from a known missing
  exchange session; this phase will not invent a complete exchange calendar.

### Strategy robustness

- The current chronological holdout is useful, but one split does not describe
  stability over time.
- Current metrics can look convincing with very few trades. Resampling and fold
  results must expose sample size and unavailable states.
- A parameter sweep is diagnostic, not an optimizer. The UI must not
  automatically select the best in-sample value.
- Multi-timeframe values must be aligned to the primary bar close. A higher
  timeframe bar cannot be used until its own close, otherwise the evaluator
  leaks future information.

### Portfolio risk

- The average-cost ledger reconciles transactions and current valuation, but
  concentration alone is not market risk.
- Risk calculations require aligned daily returns, explicit missing-history
  behavior, a benchmark, and stable annualization rules.
- Current allocation weights describe a snapshot. Historical portfolio-risk
  estimates based on those weights are hypothetical and must be labeled as
  such.
- Money-weighted return can use dated external cash flows. A daily time-weighted
  estimate is necessarily approximate when only end-of-day prices are
  available.

## 3. Package A — provenance and data-quality layer

### Domain additions

Add explicit, serializable domain values:

- `PriceAdjustmentMode`: raw, split-adjusted, and total-return adjusted;
- `CorporateAction`: timestamp, split or cash-dividend type, values, currency,
  and provider provenance;
- `AdjustedClosePoint`: timestamp plus provider-adjusted close;
- `MarketDataQualityReport`: source-row counts, rejected rows, duplicate rows,
  suspicious gaps, outliers, zero-volume bars, grade, and bounded issues.

`MarketDataMetadata` records the requested and actually applied adjustment
mode. If a provider cannot supply the required information, the result remains
raw and includes a visible warning; it is never mislabeled as adjusted.

### Provider parsing and transformations

- Parse Yahoo `events.dividends`, `events.splits`, and
  `indicators.adjclose`.
- Keep parser limits, finite-number validation, timestamp validation, stable
  ordering, and conflicting-duplicate rejection.
- Report rejected provider rows instead of silently discarding them.
- Split adjustment changes historical OHLC prices before the split and applies
  the inverse factor to historical volume.
- Total-return adjustment scales OHLC with the adjusted-close/raw-close factor.
  Volume stays raw because dividend adjustment is not a share-count change.
- Transformations operate on a copy; raw bars remain the canonical cache input.

### Quality rules

- Count short unexpected gaps. Long closures are reported as session/calendar
  ambiguity rather than declared missing bars.
- Flag extreme close-to-close moves unless a nearby parsed split explains them.
- Report zero-volume coverage as information because some asset classes do not
  supply centralized volume.
- Grade the series `good`, `caution`, or `poor` from deterministic thresholds.
- Bound the issue list and keep aggregate counts even when individual examples
  are truncated.

### UI and cache rules

- Add a desktop price-basis selector and persist it.
- Show actual price basis, corporate-action counts, quality grade, accepted and
  rejected rows, gaps, and outliers in Data Status.
- Refresh the provider when price basis changes; never transform demo or CSV
  data without inputs.
- Cache only raw polled bars in the existing schema. Adjusted views remain
  derived, preventing cache-key contamination.
- Comparison data uses the same requested basis and shows the basis actually
  applied by its provider.

### Gate

Fixtures cover Yahoo dividends, splits and adjusted close, partial/malformed
rows, conflicting duplicates, split and total-return arithmetic, gap/outlier
classification, and unsupported adjustment fallback. Existing CSV, Twelve
Data, mobile, cache, and chart behavior remains valid.

## 4. Package B — strategy robustness workbench

### Multi-timeframe conditions

- Extend each strategy operand with an optional source timeframe.
- Migrate strategy JSON from schema 1 to schema 2; old strategies map to the
  current chart timeframe.
- Add source-timeframe controls for both sides of every condition.
- Load required raw completed series from the provider-attributed cache.
- Align values at the primary bar close. A source bar is available only when
  `source timestamp + source duration <= primary timestamp + primary duration`.
- Return an explicit unavailable result when a requested series or warm-up is
  missing. Never fall back to the primary timeframe.

### Robustness reports

- Walk-forward: run several chronological test folds with earlier data retained
  only as warm-up; report every fold, median return, positive-fold percentage,
  and worst fold.
- Monte Carlo: deterministically resample observed net trade returns; report
  median, 5th/95th percentile terminal returns, drawdown percentile, and loss
  probability. Reject samples with too few trades.
- Parameter stability: sweep a bounded neighborhood around the first
  period-bearing operand and report all outcomes without auto-selecting a
  winner.
- Regime analysis: classify entry-time trades by trend and realized-volatility
  regime, then report count, win rate, average return, and net P/L.
- Multi-symbol validation: run the unchanged strategy over compatible cached
  watchlist series and show unavailable symbols separately.

### UI

Add a `Robustness` tab to Strategy Lab with bounded fold/simulation controls,
one explicit Run action, summary labels, and fold/parameter/regime/symbol
tables. Results state their sample sizes and assumptions.

### Gate

Tests prove no-lookahead timeframe alignment, schema migration, fold
boundaries, deterministic Monte Carlo percentiles, parameter non-mutation,
regime reconciliation, and explicit missing-series results.

## 5. Package C — portfolio risk and rebalancing

### Persistent targets

- Migrate portfolio JSON from schema 1 to schema 2.
- Add validated, unique symbol targets in `[0, 100]`, with total target
  allocation no greater than 100 percent.
- Add target editing and a rebalancing table.
- Suggested changes are value differences and approximate shares only. They do
  not create transactions or orders.

### Return and risk engine

Use completed raw daily cached bars for every priced holding and a user-selected
benchmark:

- align returns by UTC trading date and report common-observation coverage;
- calculate fixed-current-weight annualized return and volatility, maximum
  drawdown, Sharpe ratio at a documented zero risk-free rate, historical 95
  percent VaR and CVaR;
- calculate benchmark beta and annualized alpha from the aligned sample;
- calculate pairwise holding correlations with pair-specific observation
  counts;
- calculate covariance-based marginal risk contributions on the common sample;
- calculate XIRR from deposits, withdrawals, and ending equity;
- calculate a labeled daily-close TWR estimate only when daily valuations are
  complete around every external cash-flow date.

No metric is emitted when its mathematical prerequisites are missing or
degenerate.

### UI

Add `Risk` and `Targets` tabs to the portfolio panel. Display the benchmark,
observation window, coverage, fixed-weight assumption, incomplete symbols,
summary metrics, correlations, risk contributions, and suggested rebalance.

### Gate

Tests cover exact return alignment, constant/degenerate series, beta/alpha,
VaR/CVaR tail direction, correlation symmetry, contributions summing to total
risk, XIRR sign requirements, target validation/migration, and incomplete
rebalancing prices.

## 6. Cross-package logic invariants

1. The requested price basis and applied price basis are distinct values.
2. Raw and adjusted bars are never stored under the same cache identity.
3. Strategy and portfolio analytics use completed bars only.
4. Higher-timeframe values become visible only after their bar closes.
5. Every table distinguishes `no signal`, `zero`, and `unavailable`.
6. Percentiles use a documented deterministic interpolation rule.
7. Annualization requires enough elapsed time or enough daily observations.
8. Provider, symbol, timeframe, price basis, sample count, and date range stay
   visible beside results.
9. A robustness or risk calculation never mutates saved strategy, transaction,
   target, cache, or provider data.
10. No rebalance suggestion creates an order or paper transaction.

## 7. Verification and release

- Add focused unit tests for all new pure calculations and migrations.
- Keep the WebEngine smoke test network-independent.
- Run Release configure/build, all CTest targets, and the direct web tests.
- Run Windows deployment and isolated smoke testing.
- Build the unsigned Android ARM64 artifact to prove core changes remain
  portable; signing remains a separate explicit operation using the existing
  untracked key.
- Update README, About text, implementation plan, version metadata, workflow
  artifact names, and package scripts to 0.7.0.
- Perform a final code review and a final logic review after implementation.
