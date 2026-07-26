# Detailed implementation plan

Status: original offline MVP completed on 2026-07-24; reviewed online-provider
extension implemented on 2026-07-25; data-intelligence workstation package
implemented on 2026-07-26; Android package implemented and emulator-tested on
2026-07-26; Phase 10 Strategy Lab, local history cache, and boundary hardening
implemented and package-verified on 2026-07-26; Phase 11 data integrity,
strategy robustness, and portfolio-risk packages implemented on 2026-07-26;
Phase 12 point-in-time fundamentals, screening, valuation, and event-impact
packages implemented on 2026-07-26; Phase 13 evidence-based risk and context
analysis implemented on 2026-07-26; Phase 14 no-repaint market structure and
pattern intelligence implemented on 2026-07-26.

## 1. Goal and non-goals

Build Windows and Android chart applications in C++20 and Qt with the
interaction quality of the `TradeViewer_alt` chart, replacing its custom
QPainter renderer with the open-source TradingView Lightweight Charts library.

The application preserves offline operation while using direct market-data
providers when a network is available:

- it opens without credentials or network access;
- it requests Yahoo Finance chart data by default;
- it can fall back to Twelve Data when an environment-provided key is present;
- it provides deterministic demo data;
- it imports local OHLCV CSV files; and
- it has no Alpaca code or broker coupling.

Non-goals are real-money trading, scraping TradingView, unofficial TradingView
data endpoints, app-store publication, broker connectivity, short selling,
background/mobile alerts, automatic parameter optimization, and paper order
execution. Yahoo's chart endpoint is explicitly labeled as unofficial and is
never presented as a TradingView data service.

## 2. Reviewed findings

### Lightweight Charts boundary

Lightweight Charts 5.2.0 is a TypeScript/JavaScript HTML5-canvas renderer. Its
API consumes application-provided series data through `setData()` and `update()`.
It is not a market-data client and does not supply quotes, history, symbols,
broker access, or exchange permissions.

Therefore:

- no-server mode means local/demo/replay data only;
- direct-to-provider mode can avoid an application-owned backend but still
  contacts a third-party server and may need an API key; and
- any future provider must be an explicit adapter outside the chart renderer.

### Reference application boundary

`TradeViewer_alt` is a C++20 project with a provider-neutral core, Qt Widgets
desktop shell, Qt Quick mobile shell, SQLite persistence, a custom desktop
renderer, and Alpaca REST/WebSocket adapters. The new project reuses the useful
architectural separation and workstation interaction ideas, not the Alpaca
credential or transport implementation.

### Integration choice

Do not port or rewrite Lightweight Charts in C++. Host the supported JavaScript
library in `QWebEngineView`, package all web assets locally, and use
`QWebChannel` as a typed boundary between C++ and JavaScript.

## 3. Architecture

```text
Qt MainWindow
  ├── Named watchlists / timeframe / style / scale / theme controls
  ├── Configurable multi-indicator and data-status docks
  ├── Strategy Lab
  │     ├── shared conditions / next-bar backtest
  │     ├── deterministic replay
  │     └── cached scanner / foreground alerts
  ├── Fundamental Lab
  │     ├── SEC CompanyFacts / point-in-time SQLite
  │     ├── reported and derived series / scenario valuation
  │     ├── local multi-factor screen / same-unit peers
  │     └── event impact / opt-in foreground alerts
  ├── MarketDataClient
  │     ├── Yahoo Finance (primary)
  │     └── Twelve Data (optional fallback)
  ├── DemoDataSource ─┐
  ├── CsvBarLoader ───┼── std::vector<Bar>
  ├── TechnicalIndicators / WatchlistWorkspace
  ├── HistoricalDataStore (provider-attributed SQLite)
  └── ChartView
        ├── ChartBridge (QWebChannel)
        └── local index.html + chart.js
              └── Lightweight Charts 5.2.0
```

### C++ domain and sources

- `Bar`: Unix-second timestamp plus open/high/low/close/volume.
- `validateBars`: finite values, positive prices, OHLC consistency, strictly
  increasing timestamps, and non-negative volume.
- `DemoDataSource`: deterministic seeded random walk for offline development.
- `CsvBarLoader`: case-insensitive header mapping, quoted CSV fields, Unix or
  ISO time parsing, line-specific diagnostics, and final series validation.
- `MarketDataParser`: provider-specific JSON conversion into validated `Bar`
  values without coupling provider schemas to the renderer.
- `MarketDataClient`: asynchronous HTTPS requests, cancellation, request
  timeouts, Yahoo-first fallback sequencing, and environment-only credentials.
- `TechnicalIndicators`: validated configurable SMA, EMA, VWAP, RSI, MACD,
  rolling high/low, and Volume SMA calculations over the displayed bars.
- `WatchlistWorkspace`: validated named lists, local notes, JSON persistence,
  deterministic sorting, and quoted CSV import/export.

### Qt bridge and presentation

- `ChartBridge` owns the last successfully validated series and presentation
  state.
- `QWebChannel` signals replace the full series, change theme/style, and request
  fit-to-content.
- JavaScript reports readiness and errors back to C++.
- No untrusted remote navigation is allowed inside the embedded view.
- The default profile is memory-only; there is no browser cache or persistent
  cookie dependency.

### Web renderer

- Load the pinned standalone production bundle from a Qt resource.
- Keep TradingView's `attributionLogo` enabled.
- Render price plus volume.
- Support candlestick, line, and area modes without reloading the page.
- Support Linear, Log, and Percent modes without reloading the page.
- Render multiple overlays and independent oscillator panes.
- Present safe text-only crosshair OHLCV, relative-volume, and indicator details.
- Preserve pan/zoom for incremental presentation changes; fit only on explicit
  load/reset.
- Escape all text rendered into HTML and accept data only through WebChannel.

## 4. Delivery phases and gates

### Phase 0 — repository and compliance

Deliver CMake skeleton, MIT license, upstream Apache license and NOTICE,
third-party notice, pinned npm metadata, `.gitignore`, and this plan.

Gate: the repository explains that the chart library does not provide data and
documents the required TradingView attribution.

### Phase 1 — domain and offline data

Deliver `Bar`, validation, deterministic demo generation, CSV parser, fixtures,
and unit tests.

Gate: malformed OHLC, duplicate/out-of-order timestamps, bad numbers, missing
headers, and valid Unix/ISO rows are covered.

### Phase 2 — chart integration

Deliver the WebEngine view, WebChannel bridge, local resources, price/volume
series, style/theme switching, fit action, and error forwarding.

Gate: the page acknowledges readiness, loads at least 500 bars, and has no CDN
or runtime network request.

### Phase 3 — workstation shell

Deliver the watchlist, symbol/timeframe controls, source/status labels, file
picker, settings persistence, About/attribution UI, and keyboard shortcuts.

Gate: all controls operate on both demo and imported data without restarting
the app.

### Phase 4 — verification and packaging

Deliver CTest coverage, `--smoke-test`, Qt bootstrap, Windows deployment script,
clean checkout instructions, and GitHub CI configuration.

Gate: configure, build, unit tests, WebEngine smoke, and packaging pass from a
clean checkout with the documented Qt kit.

### Phase 5 — publication

Create a private GitHub repository, commit the reviewed scope, and push through
the user's existing GitHub identity. Keep `node_modules`, build outputs, local
Qt SDKs, and generated packages out of version control.

Gate: remote default branch contains the source, pinned dependency metadata,
vendored runtime bundle, notices, plan, and passing local verification.

### Phase 6 — direct market data

Deliver Yahoo Finance as the no-key default, optional Twelve Data fallback via
`TWELVE_DATA_API_KEY`, provider response parsers, active-symbol refresh,
cancellation, visible source labels, offline fallback, and a non-CI live
diagnostic mode.

Gate: provider parser fixtures pass, `--market-data-smoke-test` retrieves and
validates AAPL from Yahoo, normal WebEngine smoke remains network-independent,
and provider failure leaves the application usable with demo data.

### Phase 7 — data-intelligence workstation

Deliver a crosshair information strip, provider/freshness status, three price
scale modes, persistent editable named watchlists with CSV exchange, rolling
high/low and Volume SMA, and configurable simultaneous indicators.

Gate: calculation and watchlist tests pass; the WebEngine smoke test exercises
several indicator panes and Log/Percent scales; polled, local, and synthetic
sources remain visibly distinct; and the deployable Windows package passes its
isolated runtime smoke test.

### Phase 8 — provenance-aware research and margin risk

Deliver normalized company snapshots, dated organization targets, source- and
confidence-aware research events, local persistence, target CSV exchange,
optional manual Alpha Vantage overview/earnings refresh, and a transparent
long-only margin-maintenance stress calculator.

Gate: organization consensus uses only the latest target per organization and
never mixes currencies or provider aggregates; parser, persistence, CSV, and
margin formula tests pass; provider calendar failure preserves a valid
overview; the UI never describes an estimated earnings date as confirmed or a
calculated margin threshold as a future margin-call date.

### Phase 9 — Android APK

Deliver a Qt Quick Android shell, local Android WebView chart renderer, native
Android HTTPS adapter, responsive system-bar handling, minimal permissions,
launcher branding, deterministic offline fallback, signed ARM64 packaging, and
GitHub Actions APK verification.

Gate: the x86_64 debug build installs and retrieves validated Yahoo data in an
API 36 emulator; the signed ARM64 release APK passes `apksigner`; the manifest
contains Internet access but no location/storage permissions; the renderer and
license assets are packaged locally; and GitHub Releases provides the APK and
SHA-256 checksum.

### Phase 10 — local Strategy Lab and boundary hardening

Deliver a provider-attributed SQLite history cache, one reusable condition
model, long-only next-bar backtesting, deterministic bar replay, cached
watchlist scanning, and foreground local alerts. Also close the release and
input-boundary findings from the Phase 9 review.

The reusable rule vocabulary is deliberately small:

- fields: close, volume, SMA, EMA, RSI, and volume ratio;
- comparisons: greater than, less than, crosses above, and crosses below;
- right-hand values: another field or a finite constant; and
- groups: require all conditions or any condition.

The engine supports multiple entry and exit conditions even though the first
desktop editor presents one entry rule and one exit rule. This keeps the
storage and evaluator reusable by chart markers, alerts, scanner, replay, and
backtest without making the first UI difficult to audit.

Backtest invariants:

1. A rule is evaluated only after bar `i` closes.
2. A resulting order executes at bar `i + 1` open, never on bar `i`.
3. Entry slippage increases the execution price and exit slippage decreases it.
4. Commission is applied to both sides and an entry is rejected when its total
   cost exceeds available cash.
5. Long-only quantity and cash can never be negative.
6. An open final position is marked to the last close and recorded as a forced
   close, so metrics and the trade list reconcile to final equity.
7. The equity curve includes idle cash and the marked value of an open
   position. Maximum drawdown is calculated from that complete curve.
8. The engine never synthesizes missing bars or interprets provider gaps as
   zero returns.

Cache invariants:

1. The primary key is provider, normalized symbol, timeframe, and timestamp.
2. New downloads upsert overlapping bars inside one transaction and do not
   delete older non-overlapping history.
3. Each series records delivery mode, exchange, currency, timezone,
   instrument type, interval, provider retrieval time, and cache update time.
4. Invalid bars, unknown timeframes, empty provider/symbol identities, and
   non-finite values are rejected before a transaction starts.
5. Cache reads are ordered and revalidated through the domain validator.
6. Synthetic demo data is not persisted as provider history.

Scanner and alert invariants:

1. Scans use only each series' latest completed bar.
2. A symbol with missing history or insufficient indicator warm-up produces an
   explicit unavailable result, not a non-match.
3. Foreground alerts are edge-triggered once per symbol/bar and keep an audit
   record; they do not claim to run while the application is closed.
4. The same evaluator and condition serialization are used by the backtest,
   scanner, and alert path.

Security and release invariants:

1. Android signing material is never generated implicitly. A new key requires
   an explicit initialization switch, and partial key material is always an
   error.
2. Debug or unsigned APKs use distinct names and cannot overwrite the canonical
   signed release artifact.
3. Network and CSV payload limits are enforced while reading, before an
   unbounded in-memory copy exists.
4. Spreadsheet exports neutralize formula-leading text.
5. Imported target prices use the same bounded range as the UI, and even-count
   medians avoid overflow.

Gate: cache round-trip, overlap upsert, provenance, evaluator warm-up/crossing,
next-bar execution, costs, drawdown, replay boundaries, scanner unavailable
states, alert de-duplication, signing failure modes, bounded inputs, and formula
neutralization have automated coverage. The Windows package passes its isolated
WebEngine smoke test, the Android release still builds and signs with the
existing key, and the live Yahoo diagnostic remains valid.

### Phase 11 — professional validation and portfolio risk

Implement the reviewed
[`PHASE_11_PROFESSIONAL_VALIDATION_PLAN.md`](PHASE_11_PROFESSIONAL_VALIDATION_PLAN.md):
explicit raw/split/total-return price bases, parsed corporate actions, bounded
data-quality diagnostics, no-lookahead multi-timeframe strategy conditions,
walk-forward/Monte Carlo/parameter/regime/multi-symbol robustness reports, and
history-aligned portfolio risk, money-weighted return, allocation targets, and
non-executing rebalance suggestions.

Gate: requested and applied price basis never diverge silently; adjusted bars
cannot contaminate the raw history cache; robustness results expose sample
sizes and unavailable states; portfolio risk exposes coverage and fixed-weight
assumptions; schema-1 strategies and portfolios migrate without data loss; all
desktop, web, Windows-package, and Android portability gates pass.

### Phase 12 — point-in-time fundamentals and event impact

Implement the reviewed
[`PHASE_12_POINT_IN_TIME_FUNDAMENTAL_PLAN.md`](PHASE_12_POINT_IN_TIME_FUNDAMENTAL_PLAN.md):
official SEC CompanyFacts acquisition, accession-level provenance, a separate
SQLite fact cache, filing-date-bounded annual/quarterly/TTM reconstruction,
transparent derived metrics, DCF/reverse-DCF scenarios, same-unit cached peer
comparisons, a saved local fundamental/technical/event screen, and descriptive
earnings/filing impact reports with optional foreground alerts.

Gate: no future filing or price can enter a historical screen; cumulative
quarters and restatements resolve deterministically; price-dependent values
require an as-of raw daily close; peer values never mix units; a ticker/CIK
change cannot mix issuer facts; all unavailable inputs stay explicit; and the
native, renderer, Windows-package, and Android portability gates pass.

### Phase 13 — evidence-based risk and context analysis

Implement the reviewed
[`PHASE_13_RISK_CONTEXT_ANALYZER_PLAN.md`](PHASE_13_RISK_CONTEXT_ANALYZER_PLAN.md):
a deterministic daily-chart risk score, benchmark-regime context, explainable
candle/trend/volatility/location/event/fundamental evidence, constructive
counter-evidence, coverage disclosure, and no-lookahead historical comparable
outcomes.

Gate: every score reconciles to capped category points; unavailable categories
reduce visible coverage rather than implying safety; historical examples use
the same category mask and only point-in-time inputs; small samples are never
called probabilities; and native, renderer, Windows-package, and Android
portability gates pass.

### Phase 14 — market structure and pattern intelligence

Implement the reviewed
[`PHASE_14_MARKET_STRUCTURE_PLAN.md`](PHASE_14_MARKET_STRUCTURE_PLAN.md):
confirmed pivots, ATR-normalized support/resistance zones, trendlines and
channels, bounded classic-pattern detection, completed higher-timeframe
confluence, and descriptive point-in-time pattern outcomes. Render the
validated structures through a local Lightweight Charts series primitive and
reuse the evidence in Risk & Context.

Gate: formation timestamps use only confirmed anchors and completed-close
breakouts; historical outcomes begin at the detection close; the current
incomplete higher-timeframe bucket is excluded; outputs and overlays remain
bounded; and native, renderer, Windows-package, live-data, and signed Android
portability gates pass.

## 5. Remaining follow-on roadmap

After version 1.0 is stable:

1. Generalize the implemented `MarketDataClient` behind `IMarketDataSource`
   when a third provider or streaming transport is added.
2. Reassess provider licensing, redistribution, authentication, and retention
   rules before public or commercial distribution.
3. Add incremental `update()` streaming, gap detection, reconnect, and bounded
   memory.
4. Expand symbol-scoped horizontal levels into trend lines and other drawing
   primitives with selection, undo/redo, and named multi-workspace presets.
5. Add durable scheduled/background notifications only after OS-specific
   lifecycle and permission handling are implemented.
6. Extend the implemented SEC/FRED adapters and CompanyFacts concept map only
   after each additional form, concept, or release has an explicit accounting
   mapping and provenance policy; neither source supplies analyst price
   targets.
7. Add portfolio-level strategy backtests, short selling, rolling-factor
   attribution, and configurable risk-free-rate curves only after execution,
   borrow-cost, calendar-alignment, and annualization rules are explicit.
8. Extend the local average-cost paper ledger with broker-specific tax lots or
   execution simulation only after quote freshness and audit requirements are
   defined.
9. Add a Play Store App Bundle only after a stable signing-key backup,
    store-listing privacy disclosures, provider-terms review, and device-matrix
    testing.

## 6. Risks and mitigations

- **False data-source assumption:** state the renderer/data separation in UI and
  docs; never add hidden or scraped TradingView endpoints.
- **WebEngine package size:** keep it explicit in prerequisites and deployment;
  do not replace it with an unsupported C++ port.
- **Android WebEngine availability:** use Qt WebView with the maintained system
  WebView because Qt WebEngine is desktop-only.
- **Android TLS maintenance:** use Android's HTTPS stack and device trust store
  instead of bundling an aging OpenSSL binary.
- **Signing-key loss:** keep the key outside Git and require a secure backup;
  existing installations cannot accept upgrades signed by a replacement key.
- **C++/JavaScript type drift:** centralize JSON conversion in `ChartBridge` and
  test timestamp/number constraints.
- **License regression:** pin the version, vendor upstream LICENSE/NOTICE, keep
  `attributionLogo` on, and review notices during dependency upgrades.
- **CSV ambiguity:** require named columns, UTC-normalize timestamps, reject
  ambiguous invalid rows rather than silently guessing.
- **Unofficial Yahoo dependency:** label it accurately, enforce timeouts, keep
  Twelve Data and offline fallbacks, and avoid depending on it for startup.
- **Provider scope creep:** keep acquisition outside the renderer, poll only
  the active symbol, isolate parser tests, and require a rights review before
  distribution.

## 7. Definition of done

The extended application is done when a new clone can bootstrap Qt, configure
and build, pass unit tests, complete the network-independent WebEngine smoke
test, retrieve the active symbol from Yahoo, use Twelve Data when configured,
fall back offline, import and validate CSV data, switch
timeframe/style/theme, show TradingView attribution, package on Windows, and run
without Alpaca credentials or an application-owned backend. Version 0.4 also
requires persistent named watchlists, conservative source/freshness labels,
crosshair details, configurable multi-indicators, Linear/Log/Percent scale
switching, provenance-aware research records, latest-per-organization target
summaries, an explicitly estimated event calendar, and a transparent
margin-maintenance scenario. The Android APK must additionally run live Yahoo
data in an emulator, use only expected permissions, and verify with
`apksigner`. Version 0.5 also requires the Phase 10 cache and analytics
invariants above, clear foreground-only alert wording, and reproducible
reconciliation between each backtest trade list and its final equity.
Version 0.6 additionally requires bounded and provenance-preserving SEC/FRED
event acquisition, multi-condition strategy editing and holdout metrics,
restart-safe local alert persistence, reconciled average-cost paper portfolios,
exact-timestamp comparison statistics, synchronized comparison charts, and
symbol-scoped price levels. Every unavailable or partial result must remain
explicit rather than being presented as a complete valuation, correlation, or
real-time service.
Version 0.7 additionally requires explicit requested/applied price bases,
quality diagnostics without adjusted-cache contamination, no-lookahead
multi-timeframe strategy evaluation, reproducible robustness reports,
history-aligned portfolio risk, schema migration, and non-executing allocation
targets. Desktop, web, deployable Windows, and Android portability gates must
all pass.
Version 0.8 additionally requires accession-preserving SEC CompanyFacts
storage, filing-date and price-date point-in-time boundaries, deterministic
annual/quarterly/TTM reconstruction, transparent ratio and DCF inputs,
same-unit peer comparison, a locally cached current-universe screen,
descriptive event-impact windows, and de-duplicated opt-in foreground
fundamental alerts. Missing facts, prices, comparable units, or benchmark
history must remain explicit.
Version 0.9 additionally requires an explainable observed-risk assessment,
benchmark and input coverage, dated evidence, point-in-time historical
comparables, and explicit small-sample and educational-use limitations.
Version 1.0 additionally requires confirmed market-structure timestamps,
bounded local overlays, completed higher-timeframe confluence, point-in-time
pattern outcomes, and deterministic reuse by the Risk & Context analyzer.
