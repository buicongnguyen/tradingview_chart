# Phase 12 — point-in-time Fundamental, Screening, and Event-Impact Lab

Status: implemented and reviewed on 2026-07-26 for version 0.8.0.

## 1. Objective

Add a professional desktop fundamental-research workflow without scraping
TradingView, inventing estimates, connecting to a broker, or weakening the
existing provider boundaries. The package uses the official SEC EDGAR
CompanyFacts API for reported issuer facts and combines those facts locally
with provider-attributed raw daily price history and the existing
provenance-aware research calendar.

The package contains:

- a point-in-time SEC CompanyFacts parser and separate SQLite cache;
- annual, quarterly, and trailing-twelve-month fundamental series;
- transparent growth, margin, cash-flow, leverage, return, and valuation
  calculations;
- a scenario DCF and reverse-DCF growth estimate;
- same-unit cached-company comparisons;
- a saved local multi-factor screener;
- earnings and filing event-impact reports; and
- opt-in foreground alerts for a newer SEC accession or a current screen
  match.

## 2. Reviewed boundaries

### Data-source boundary

TradingView Lightweight Charts remains a renderer and supplies no market or
fundamental data. SEC EDGAR supplies issuer-reported facts, not analyst target
prices, consensus forecasts, live quotes, exchange calendars, or investment
recommendations. Yahoo/Twelve Data price history, manually recorded
organization targets, and research events retain their own identities and are
never relabeled as SEC data.

### Point-in-time boundary

An SEC fact is eligible only when its `filed` date is on or before the chosen
as-of date. Period end, fiscal year, or frame never substitutes for public
availability. A historical screen also truncates raw daily prices before
calculating price, RSI, SMA distance, volatility, or price-dependent
valuation. This prevents future filings and prices from leaking into a past
screen.

### Accounting boundary

CompanyFacts uses issuer-selected US-GAAP or IFRS tags and may contain
duplicates, comparative facts, amendments, restatements, non-calendar fiscal
years, and cumulative year-to-date values. The implementation therefore keeps
taxonomy, tag, unit, period, filing date, form, fiscal metadata, accession,
frame, source URL, and retrieval time for every accepted fact. Unsupported
concepts are not guessed.

### Comparison boundary

Absolute peer values are shown only when their reported units match the
selected company's unit. The application performs no hidden currency
conversion. The screen uses the current locally cached watchlist universe and
is explicitly not a survivorship-bias-free historical universe.

## 3. Architecture

```text
Official SEC endpoints
  ├── company_tickers.json ── ticker → CIK
  └── CompanyFacts JSON
          │ bounded HTTPS + contact-bearing SEC_USER_AGENT
          ▼
SecCompanyFactsParser
  ├── identity / form / numeric / provenance validation
  ├── supported tag mapping
  └── canonical fact ordering and de-duplication
          ▼
FundamentalStore (fundamentals.sqlite)
          │
          ├── point-in-time annual / quarterly / TTM series
          ├── derived ratios and DCF
          ├── same-unit cached peer view
          └── local screen
                  ▲
                  ├── raw daily history.sqlite prices/technicals
                  └── research events and organization targets

Research event + raw daily history
          └── event alignment, gap, volume, +1/+5/+20 returns,
              benchmark return, and descriptive abnormal return
```

Fundamental storage is intentionally separate from price history. A ticker
whose resolved CIK changes is replaced atomically so facts from two issuers
cannot be mixed.

## 4. Detailed implementation packages

### Package A — SEC acquisition, parsing, and persistence

1. Require a valid normalized symbol and contact-bearing `SEC_USER_AGENT`.
2. Resolve the official SEC ticker map on every manual refresh so a ticker
   reassignment cannot silently keep using a stale cached CIK.
3. Bound ticker-map responses at 12 MiB and CompanyFacts responses at 32 MiB.
4. Accept supported 10-K, 10-Q, 20-F, 40-F, and 6-K forms and amendments.
5. Map only explicitly supported US-GAAP/IFRS concepts.
6. Reject invalid identities, periods, filing dates, values, accessions,
   sources, and oversized fact collections.
7. Persist at most 50,000 accepted facts with an indexed point-in-time query
   path.

Gate: parser fixtures, cache round-trip, source preservation, malformed-fact
rejection, and ticker/CIK identity isolation pass.

### Package B — point-in-time reconstruction

1. Prefer the latest public filing for a period, using tag priority only as a
   deterministic tie-breaker within the same availability boundary.
2. Match comparative facts using actual period start/end windows rather than
   assuming SEC `fy` is the reporting year.
3. Preserve direct fiscal quarters.
4. Reconstruct additive Q2 and Q3 values from compatible cumulative
   year-to-date facts.
5. Reconstruct Q4 from the compatible annual value less the first three
   quarters.
6. Sum four compatible quarters for TTM; expose the maximum contributing
   filing date and mark reconstructed values.
7. Never synthesize a missing accounting concept.

Gate: non-calendar metadata, cumulative-quarter reconstruction, annual Q4,
restatement choice, TTM, and no-lookahead cases are deterministic.

### Package C — derived analytics and scenario valuation

The implemented calculations are:

```text
gross margin       = TTM gross profit / TTM revenue
operating margin   = TTM operating income / TTM revenue
net margin         = TTM net income / TTM revenue
free cash flow     = TTM operating cash flow - abs(TTM capital expenditure)
FCF margin         = TTM free cash flow / TTM revenue
long-term D/E      = latest mapped long-term debt / latest equity
ROE                = TTM net income / latest equity
market cap         = as-of raw daily close × diluted shares
P/E                = as-of close / TTM diluted EPS
P/S, P/B, P/FCF    = market cap / matching reported value
```

ROE uses ending equity rather than average equity and is labeled as a derived
screening measure. “Debt” means the mapped long-term debt/lease concepts, not a
complete net-debt taxonomy.

The DCF starts from positive TTM free cash flow, applies a user-selected
constant forecast growth rate for 1–20 years, discounts each forecast, applies
a Gordon-growth terminal value, subtracts mapped long-term debt net of cash,
and divides by diluted shares. The reverse DCF solves the constant annual FCF
growth that reconciles the same model to the as-of market price. It is a
scenario, not an analyst target.

Gate: invalid assumptions and missing inputs fail explicitly; DCF,
sensitivity, and reverse-growth results are finite and covered by tests.

### Package D — local screen and peer comparison

1. Persist up to 16 AND-combined conditions and a sort definition.
2. Support growth, margins, FCF, leverage, ROE, valuation multiples, RSI,
   SMA50 distance, 20-day volatility, days to earnings, and organization-target
   upside.
3. Use only a raw daily bar on or before the as-of date.
4. Select organization targets using the latest record per organization and
   currency published by the as-of date.
5. Report unavailable values separately from failed conditions.
6. Compare absolute peer values only within the selected reported unit.

Gate: screen serialization, invalid enums and thresholds, technical warm-up,
future-price exclusion, coverage, stable sorting, and same-unit policy pass.

### Package E — event impact and foreground alerts

1. Accept cached earnings and filing events.
2. Align an event to the first UTC trading date on or after its supplied date,
   or strictly after it when the user marks the event as after-market-close.
3. Calculate the opening gap, event-day close return, event volume divided by
   the prior 20-day average, and +1/+5/+20 trading-day returns from the prior
   close.
4. When exact benchmark dates exist, subtract benchmark returns and label the
   result descriptive rather than causal.
5. Notify only when explicitly enabled:
   - a refresh finds an accession not present in the prior cache at or after
     the prior latest filing date; or
   - a current-date saved screen matches a symbol, once per symbol/price date.
6. Reuse the existing bounded local alert audit and foreground system-tray
   delivery. No alert claims to run while the desktop process is closed.

Gate: weekend alignment, insufficient post-event windows, benchmark coverage,
and restart-safe external-alert de-duplication pass.

## 5. Logic-review findings fixed during implementation

- Comparative SEC facts were initially grouped by the provider `fy` field;
  they now match actual reporting windows, which handles restated comparative
  facts and non-calendar metadata more safely.
- Tag priority initially outranked filing date; the latest public filing now
  wins before tag priority so amendments and restatements are not hidden.
- Historical screens initially used the newest cached price; all price and
  technical calculations now stop at the selected as-of date.
- Historical summary valuation initially could use the current chart close;
  it now requires a provider-attributed raw daily close on or before the as-of
  date.
- Peer ranking initially allowed incomparable currencies; it now requires an
  exact reported-unit match.
- A date-only event could treat an after-market-close release as same-session;
  the event lab now exposes an explicit next-session alignment control.
- Reusing a ticker after a CIK change could retain old facts; the store now
  deletes the prior issuer row and its facts inside the same transaction.
- Screen enum values were not validated at the domain boundary; fields,
  operators, thresholds, and sort identity are now validated.

## 6. Release gate

Version 0.8.0 is complete when:

- all native and renderer tests pass;
- the full desktop Release target builds with warnings enabled;
- the isolated Windows package contains Qt WebChannel, WebEngine, SQL,
  platform, TLS, and resource dependencies and passes `--smoke-test`;
- missing SEC identity, price coverage, facts, benchmark history, or accounting
  concepts remain explicit unavailable states;
- no API key, SEC contact identity, analyst estimate, provider result, or
  signing key is persisted in source or application settings; and
- Android still compiles the shared core even though the Fundamental Lab
  remains a desktop workstation feature.
