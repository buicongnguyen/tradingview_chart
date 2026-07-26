# Phase 13 — evidence-based risk and context analyzer

Status: approved implementation scope for version 0.9.

## Goal

Answer the practical question “does the current daily chart look risky to buy,
and why?” with reproducible calculations. The result is an observed-risk
assessment for a configurable 5–20 trading-day horizon, not a recommendation,
forecast, or probability of loss.

## Inputs and boundaries

- The analyzed security uses completed raw daily bars from the local
  provider-attributed history cache.
- A configurable benchmark, defaulting to `SPY`, supplies market-regime
  context. Missing benchmark history lowers coverage; it is never treated as a
  healthy market.
- SEC facts are filtered by filing date through the existing point-in-time
  fundamental engine.
- Research events are usable only when their `asOfUtc` is no later than the
  analysis date. An empty calendar is unknown coverage, not evidence that no
  event exists.
- Data-quality warnings and stale series are surfaced in the report.
- The analyzer never uses TradingView data, broker data, generative AI, or
  future bars when scoring a historical setup.

## Transparent scoring model

Adverse evidence is grouped into capped categories:

| Category | Maximum points |
| --- | ---: |
| Benchmark regime | 20 |
| Trend and momentum | 20 |
| Latest candle and price action | 15 |
| Volatility and liquidity | 15 |
| Price location and extension | 10 |
| Known near-term events | 10 |
| Point-in-time fundamentals | 10 |

The displayed 0–100 score is the observed adverse points divided by the
available category weight. Coverage is shown separately. Constructive
counter-evidence is displayed but does not silently cancel adverse evidence.
Each evidence row includes its category, points, measured value, explanation,
source, and as-of date.

Risk bands are descriptive:

- 0–24: lower observed risk
- 25–44: moderate observed risk
- 45–64: elevated observed risk
- 65–100: high observed risk

No score is produced with fewer than 60 valid daily security bars. Categories
without sufficient inputs are excluded from both numerator and denominator and
listed as missing.

## Historical validation

For each eligible historical date:

1. use only bars ending on that date;
2. filter SEC facts by filed date and events by their recorded as-of time;
3. calculate the same score and category-coverage mask;
4. retain setups in the same risk band, within ten score points, and with the
   same available-category mask; and
5. measure the following 5-day return, 20-day return, maximum close-to-close
   drawdown over 20 days, and optional benchmark-relative 20-day return.

The report shows sample count, medians, loss frequency, and worst drawdown. A
sample smaller than 30 is explicitly marked insufficient and never described
as a probability. Evaluation is bounded to the latest 750 eligible setup dates
and 2,500 daily bars so the desktop remains responsive.

## Desktop workflow

Add a `Risk & Context` dock with:

- normalized symbol, configurable benchmark, and an Analyze action;
- score, risk band, coverage, data dates, and confidence;
- adverse-evidence and counter-evidence tables;
- comparable historical-outcome statistics; and
- explicit missing-input, stale-data, and educational-use warnings.

The benchmark preference is persisted. The dock refreshes its symbol context
when the chart changes, but analysis is user-triggered because it can perform
bounded historical validation.

## Verification gate

- Unit tests cover adverse and constructive regimes, candle evidence, missing
  benchmark/fundamentals/events, point-in-time event filtering, score caps,
  no-lookahead validation, small-sample disclosure, and deterministic output.
- The desktop workflow handles missing cache data without crashing or implying
  safety.
- Existing native tests, web smoke, Windows deployment smoke, and Android core
  portability still pass.
- Documentation states the interpretation and limitations beside the output.
