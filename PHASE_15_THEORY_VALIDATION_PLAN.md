# Phase 15 — Point-in-Time Theory Validation Lab

## Objective

Add a desktop Theory Validation Lab that lets a user select a historical
moment, evaluate the current and saved strategy theories using only information
available at that moment, and compare their later 5-bar and 20-bar outcomes.
The feature reports historical evidence and data reliability; it does not
predict a probability of profit or issue a recommendation.

## User workflow

1. Open **Tools → Theory Validation Lab** or the Strategy Lab's **Theory**
   tab.
2. Choose an as-of timestamp, use the latest completed bar, or use the active
   Replay moment.
3. Choose the chronological holdout percentage, minimum sample threshold, and
   round-trip cost assumption.
4. Run the current editor theory and all saved named theories.
5. Review whether each entry rule matches at the selected moment, its sample
   size, 5/20-bar hit rates, average return, unconditional baseline, excess
   return, holdout results, Wilson confidence interval, reliability, and
   evidence classification.

## Point-in-time and outcome rules

- The primary series is truncated at the latest completed bar whose timestamp
  is at or before the selected moment.
- Strategy entry conditions are evaluated on the completed signal bar.
- Higher-timeframe operands use the existing StrategyEvaluator completion
  alignment; an unfinished higher-timeframe bar is never exposed.
- A historical occurrence begins only when the entry rule transitions from not
  matched to matched. Repeated true bars are one episode.
- Occurrences are non-overlapping through the longest configured outcome
  horizon. An episode that begins inside an existing outcome window is skipped.
- The modeled long entry is the next primary bar open. The 5/20-bar outcomes
  exit at the corresponding later close.
- The configurable round-trip basis-point cost is split equally between the
  modeled entry and exit. Commissions are excluded because this event study has
  no position-size assumption.
- Future bars are used only to measure an already-detected occurrence. They are
  never used for rule matching, sample selection, or confidence.
- The chronological holdout is the trailing portion of detected occurrences,
  not a random split.

## Metrics and interpretation

For each horizon:

- samples and positive outcomes;
- hit rate;
- average and median modeled return;
- training and holdout sample counts, hit rates, and average returns;
- 95% Wilson interval for the holdout hit rate;
- unconditional forward-return baseline and average excess return.

Reliability measures evidence coverage, not profitability:

- **Insufficient**: below the configured total sample threshold or fewer than
  eight holdout occurrences.
- **Low**: adequate minimum coverage.
- **Moderate**: at least 60 total and 15 holdout occurrences.
- **High**: at least 150 total and 30 holdout occurrences.

Evidence is:

- **Positive** when training and holdout average returns are positive, the
  holdout hit rate exceeds 50%, and overall return exceeds the unconditional
  baseline.
- **Negative** for the corresponding consistently negative result.
- **Mixed** when the signs or comparisons disagree.
- **Unavailable** when the theory or data cannot be evaluated.

The lab names a "best supported" theory only among Positive theories with at
least Low reliability. Ranking prefers reliability, then the lower bound of the
holdout hit-rate interval, then holdout average return. It never searches or
changes strategy parameters.

## Packages

### A. Core event-study engine

- Add bounded validation input/result models.
- Reuse StrategyEvaluator for rule and multi-timeframe evaluation.
- Add point-in-time truncation, transition detection, non-overlap, modeled
  outcomes, baseline metrics, holdout split, Wilson intervals, reliability,
  evidence, and deterministic ranking.

### B. Desktop lab

- Add a Theory tab to Strategy Lab.
- Add timestamp, latest/replay, holdout, sample, and cost controls.
- Add comparison table, evidence summary, and selected-theory explanation.
- Add **Tools → Theory Validation Lab** navigation.
- Persist non-sensitive control settings locally.

### C. Validation

- Prove that results through timestamp T are unchanged when bars after T are
  appended.
- Prove next-open outcome timing, transition-only occurrences, non-overlap,
  chronological holdout, costs, confidence bounds, ranking, and inadequate
  sample handling.
- Extend the native UI smoke test for Tools-menu navigation and required
  controls.
- Run all native, WebEngine, and renderer tests and rebuild the portable
  Windows package.

## Explicit exclusions

- No broker connection, order execution, live recommendation, probability of
  future profit, automatic parameter optimization, p-hacking search, short
  theory, portfolio sizing, taxes, dividends, or benchmark-symbol acquisition.
- A later phase can add SPY-relative event outcomes, multiple-testing
  correction, purged cross-validation, and cross-symbol theory aggregation.
