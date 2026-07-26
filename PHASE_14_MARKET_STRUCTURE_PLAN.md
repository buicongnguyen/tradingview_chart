# Phase 14 — market structure and pattern intelligence

Status: implemented, reviewed, and package-verified for version 1.0.

## Goal

Turn repeated visual chart inspection into deterministic, reviewable market
structure. The application should identify important zones, trend geometry,
and a deliberately small set of classic formations, explain when they became
knowable, render them locally, and describe historical outcomes without
claiming that a pattern predicts the future.

## Delivered scope

1. Confirmed pivot highs and lows.
2. ATR-normalized support and resistance zones with touch, recency, break, and
   proximity state.
3. Rising/falling trendlines and converging/parallel channel boundaries.
4. Double top/bottom, triangle, rectangle, rising/falling wedge, and
   head-and-shoulders/inverse-head-and-shoulders detection.
5. Completed-higher-timeframe trend and structure confluence.
6. Pattern-relative 5/20-bar returns and maximum adverse excursion.
7. Desktop structure tables and local Lightweight Charts overlays.
8. Structure evidence reused by the Risk & Context analyzer.

Flag and pennant recognition remains deferred until the simpler geometry has
enough validation coverage; it must not be implemented as a loose
look-alike rule.

## No-repaint and point-in-time invariants

1. A pivot anchored at bar `i` is not observable until the configured number
   of right-side bars has completed. Both anchor and confirmation timestamps
   are stored.
2. A zone cannot exist until its second independently confirmed pivot.
3. A formation's detection timestamp is the latest confirmation timestamp of
   every anchor used by its geometry, or the later breakout timestamp when the
   status requires a breakout.
4. Pattern targets and invalidation levels are measurements, not orders or
   forecasts.
5. Historical outcomes begin at the detection close and use only later bars
   for outcome measurement, never for detection or confidence.
6. Higher-timeframe bars are aggregated only from completed source bars. The
   current incomplete higher-timeframe bucket is excluded.
7. Equal-price pivot plateaus do not generate an arbitrary series of pivots:
   strict extrema are required.
8. Results are bounded to the latest 1,500 bars, 64 pivots, 12 zones, 24
   formations, and 16 rendered structures.

## Zone model

Pivots of the same type are clustered when their centers are within the larger
of:

- the configured ATR multiple at the pivot confirmation; or
- 0.15% of price.

The visible zone spans the clustered pivot prices plus a quarter ATR. Strength
uses touch count, recency, and rejection distance. A close beyond the zone by
half an ATR marks it broken. A broken resistance may be described as potential
support only after a later retest; the first implementation does not silently
flip zone type.

## Formation validation

- Double tops/bottoms require two comparable extremes, an intervening swing,
  and sufficient ATR-normalized depth.
- Rectangles require comparable paired highs and lows.
- Triangles and wedges require converging independently confirmed high and low
  boundaries.
- Head-and-shoulders requires a head separated from two comparable shoulders
  and two neckline pivots.
- A formation is `Emerging`, `Confirmed`, `Invalidated`, or `Target reached`.
  Breakout confirmation uses a completed close, not an intrabar high/low.

Every formation exposes anchor dates, direction, neckline or boundary,
invalidation, optional measured target, detection date, and plain-language
reason.

## Higher-timeframe rules

Intraday source bars aggregate to completed UTC days. Daily source bars
aggregate to completed ISO weeks. Other source timeframes use the next
available stable aggregation boundary. Confluence is:

- aligned when both timeframes have the same non-neutral structure bias;
- conflicting when their non-neutral biases oppose; or
- mixed when either is neutral or history is insufficient.

## Historical validation

Each uniquely detected formation with at least 20 later bars contributes:

- signed 5-bar return;
- signed 20-bar return;
- maximum adverse excursion over 20 bars; and
- target/invalidation outcome when those levels exist.

Results are grouped by formation and direction. Medians, positive signed
outcome frequency, target/invalidation counts, and sample size are displayed.
Fewer than 30 samples is explicitly descriptive and never presented as a
probability.

## Desktop workflow and overlays

The `Market Structure` dock provides configurable pivot strength and zone ATR
sensitivity, a completed-bar Analyze action, summary, zones, formations,
higher-timeframe confluence, and validation tables.

The renderer receives a bounded validated DTO through `QWebChannel`. A local
series primitive draws:

- translucent support/resistance rectangles;
- trendline and channel segments;
- pattern boundary segments; and
- compact labels.

No runtime script or network asset is introduced.

## Verification gate

- Automated tests cover pivot confirmation time, plateau rejection, zone
  clustering, pattern geometry, status changes, aggregation boundaries,
  outcome windows, bounds, invalid input, and deterministic output.
- A future bar cannot change a report explicitly bounded to an earlier
  analysis date.
- Risk & Context structure evidence is recomputed on each historical prefix.
- Renderer tests verify the new bridge and primitive without adding a network
  dependency.
- Native tests, WebEngine smoke, Windows deployed smoke, live Yahoo diagnostic,
  Android ARM64 compilation, and signed APK verification pass.
