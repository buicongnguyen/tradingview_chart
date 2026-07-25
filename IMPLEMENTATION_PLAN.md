# Detailed implementation plan

Status: original offline MVP completed on 2026-07-24; reviewed online-provider
extension implemented on 2026-07-25.

## 1. Goal and non-goals

Build a Windows desktop chart workstation in C++20 and Qt with the interaction
quality of the `TradeViewer_alt` desktop chart, replacing its custom QPainter
renderer with the open-source TradingView Lightweight Charts library.

The application preserves offline operation while using direct market-data
providers when a network is available:

- it opens without credentials or network access;
- it requests Yahoo Finance chart data by default;
- it can fall back to Twelve Data when an environment-provided key is present;
- it provides deterministic demo data;
- it imports local OHLCV CSV files; and
- it has no Alpaca code or broker coupling.

Non-goals are real-money trading, scraping TradingView, unofficial TradingView
data endpoints, mobile packaging, screeners, alerts, backtesting, and paper
order execution. Yahoo's chart endpoint is explicitly labeled as unofficial
and is never presented as a TradingView data service.

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
  ├── Watchlist / timeframe / style / theme controls
  ├── MarketDataClient
  │     ├── Yahoo Finance (primary)
  │     └── Twelve Data (optional fallback)
  ├── DemoDataSource ─┐
  ├── CsvBarLoader ───┼── std::vector<Bar>
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

## 5. Follow-on roadmap

After the MVP is stable:

1. Add SQLite history/workspace persistence.
2. Generalize the implemented `MarketDataClient` behind `IMarketDataSource`
   when a third provider or streaming transport is added.
3. Reassess provider licensing, redistribution, authentication, and retention
   rules before public or commercial distribution.
4. Add incremental `update()` streaming, gap detection, reconnect, and bounded
   memory.
5. Add indicators as separate series/panes (SMA, EMA, VWAP, RSI, MACD).
6. Add drawing tools and layout persistence.
7. Add paper trading only after quote freshness and audit requirements are
   defined.

## 6. Risks and mitigations

- **False data-source assumption:** state the renderer/data separation in UI and
  docs; never add hidden or scraped TradingView endpoints.
- **WebEngine package size:** keep it explicit in prerequisites and deployment;
  do not replace it with an unsupported C++ port.
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
without Alpaca credentials or an application-owned backend.
