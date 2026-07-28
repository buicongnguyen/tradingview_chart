# TradeChart Lab

An online-capable C++20/Qt 6 chart viewer for Windows and Android, powered by
[TradingView Lightweight Charts](https://github.com/tradingview/lightweight-charts).
TradeChart Lab is an independent project and is not affiliated with or endorsed
by TradingView.

Read the public, task-oriented
[Building TradeChart Lab project book](https://buicongnguyen.github.io/tradingview_chart/)
for the complete architecture, Windows and Android build, testing, Git/SSH,
release, and GitHub Pages delivery process. The book source lives under
[`docs`](docs/README.md) and is validated before every Pages deployment.

The application does **not** require Alpaca, a broker account, or an application
backend. It requests the active symbol from Yahoo Finance first. If that request
fails and `TWELVE_DATA_API_KEY` is configured, it uses Twelve Data as a
fallback. When neither provider is available, it displays deterministic offline
demo data. Local OHLCV CSV import remains available.

Lightweight Charts is only the renderer. Provider availability, freshness,
rate limits, terms, and market-data display rights still apply.

## Version 1.1 features

- Basic, Intermediate, and Advanced desktop workspaces under **Tools**. Basic
  keeps the chart, watchlist, summary, interval, and style controls; Intermediate
  adds indicators, data quality, research, risk, structure, scale, and price
  levels; Advanced adds comparison, fundamentals, margin, portfolio, and the
  Strategy Lab. **Tools → Panels** supports a saved Custom arrangement, and
  **Ctrl+Shift+0** repairs the dock layout without changing the selected panels.

- A point-in-time Theory Validation Lab under **Tools** and Strategy Lab. It
  compares the current editor and saved strategy entry theories through a
  selected completed moment, models non-overlapping next-open 5/20-bar event
  outcomes, uses a chronological holdout, reports Wilson hit-rate intervals,
  unconditional baseline/excess return, and separates sample reliability from
  positive, mixed, or negative historical evidence. It does not optimize rules
  or estimate a probability of future profit.

- Confirmed-pivot Market Structure analysis with ATR-normalized support and
  resistance zones, trendlines/channels, double tops/bottoms, triangles,
  rectangles, rising/falling wedges, head-and-shoulders variants, completed
  higher-timeframe confluence, and no-lookahead 5/20-bar outcome summaries.
  A bounded local Lightweight Charts primitive renders the same zones and
  boundaries on desktop and Android.

- A desktop Risk & Context analyzer that answers “risky to buy—why?” with a
  deterministic 0–100 observed-risk score, exact adverse and constructive
  evidence, benchmark-regime context, coverage/confidence disclosure, known
  near-term event risk, point-in-time SEC metrics, and no-lookahead comparable
  5/20-session historical outcomes. It is not a recommendation or loss
  probability.

- A desktop Fundamental Lab backed by the official SEC EDGAR CompanyFacts API:
  filing-date-bounded annual, quarterly, and TTM series; accession/source
  provenance; revenue/EPS growth; margins; FCF; mapped long-term
  debt-to-equity; ROE; and price-dependent valuation multiples.
- Scenario DCF and reverse-DCF growth analysis with editable assumptions and a
  3×3 sensitivity table. These are transparent user scenarios, not analyst
  targets or forecasts.
- A saved local current-universe screen combining SEC fundamentals, raw daily
  price/technical fields, earnings proximity, and currency-compatible
  organization-target upside. Historical screens truncate both filings and
  prices at their selected as-of date.
- Fundamental history graphs, same-reported-unit cached peer comparison, and
  earnings/filing event studies with opening gap, volume ratio, +1/+5/+20
  trading-day returns, and optional benchmark-relative returns.
- Opt-in foreground notifications for newer SEC filing accessions and
  current-date fundamental screen matches, de-duplicated through the existing
  local alert audit.

- Explicit raw, split-adjusted, and total-return price bases for Yahoo data,
  with requested-versus-applied labeling, parsed dividends/splits, rejected-row
  counts, bounded gap/outlier diagnostics, and a visible quality grade. Raw
  bars remain the only series stored in the history cache.
- No-lookahead multi-timeframe strategy operands and a robustness workbench
  covering chronological walk-forward folds, deterministic trade-return Monte
  Carlo, parameter-neighborhood stability, market regimes, and unchanged-rule
  validation across compatible cached symbols.
- History-aligned portfolio analytics with fixed-current-weight volatility,
  drawdown, Sharpe, historical VaR/CVaR, benchmark beta/alpha, correlations,
  risk contributions, XIRR, an explicitly approximate daily-close TWR,
  persistent allocation targets, and non-executing rebalance suggestions.

- A desktop Strategy Lab with named strategies, up to 16 all/any entry and exit
  conditions, completed-bar evaluation, next-open execution, commissions,
  slippage, fractional-share control, deterministic replay, cached-watchlist
  scanning, and optional training/holdout results.
- An interactive long-only Trading Simulator with a configurable starting
  balance, automatic/manual/assisted decisions, step or timed playback,
  next-open fills, mark-to-market equity and drawdown, trade/decision audit
  tables, and strictly validated local resume snapshots. It is a hypothetical
  paper environment and never sends orders to a broker.
- A Safe Script Lab that parses a bounded, documented Pine-style strategy
  subset into the same native rule model. Script text is never executed.
  Unsupported language, sizing, short, stop, limit, or order semantics are
  compile errors rather than approximations.
- Expanded backtest diagnostics: reconciled trade/equity records, drawdown,
  exposure, MAE/MFE, holding time, CAGR, Sharpe, Sortino, Calmar,
  buy-and-hold comparison, consecutive losses, and underwater duration.
- Persistent local alerts for price, indicator, and strategy conditions with
  once, once-per-bar, transition, cooldown, expiry, restart-safe state, an audit
  history, foreground system-tray notifications, and sourced event reminders.
- A local, multi-portfolio paper ledger with deposits, withdrawals, buys, sells,
  dividends, fees, splits, average-cost accounting, realized/unrealized results,
  concentration measures, event exposure, and explicit incomplete-valuation
  state when a current market price is unavailable.
- Manual SEC EDGAR filing and FRED macro-calendar refresh. Every imported event
  retains its source, retrieval time, confidence, and URL. SEC requests require
  a contact-bearing `SEC_USER_AGENT`; FRED requests require `FRED_API_KEY`.
- A two-pane desktop comparison workspace with horizontal/vertical saved
  layouts, independent benchmark acquisition, exact-timestamp normalized
  relative performance, log-return correlation, synchronized visible ranges
  and crosshairs, and symbol-scoped horizontal levels that can create crossing
  alerts.
- Candlestick, line, and area charts; linear, logarithmic, and percentage
  scales; volume; light/dark themes; detailed crosshair values; and multiple
  configurable SMA, EMA, VWAP, RSI, MACD, rolling-high/low, and Volume SMA
  calculations.
- Provenance-aware research targets and event calendar, optional Alpha Vantage
  overview/earnings refresh, a transparent long-only margin stress calculator,
  persistent named watchlists, validated CSV import/export, and a local SQLite
  historical cache. Synthetic data is never cached as market data.
- Yahoo Finance polled data by default, optional Twelve Data fallback, and a
  deterministic offline mode. There is no broker connection, runtime CDN, or
  application backend.
- A native Qt Android application for Android 9+ using the same local chart
  renderer, Android-native TLS, Yahoo/Twelve Data acquisition, and signed ARM64
  packaging. The advanced desktop docks, multi-chart workspace, and tray alerts
  are desktop features and are not represented as Android capabilities.
- Bounded network and persistence payloads, formula-safe spreadsheet exports,
  native and renderer tests, a Qt WebEngine smoke test, and CI packaging checks
  for Windows and Android.

## Market-data providers

### Yahoo Finance

Yahoo Finance is the default because its chart endpoint does not require a key.
The endpoint is unofficial and unsupported, so it can be rate-limited, changed,
or withdrawn without notice. The application labels Yahoo data explicitly,
describes it as polled rather than streaming, reports delay as unknown unless
the response supplies it, and falls back safely when a request fails.

This endpoint is not a documented public market-data API. Use it only where
your access is permitted by the current
[Yahoo Terms of Service](https://legal.yahoo.com/us/en/yahoo/terms/otos/index.html).
For commercial distribution or a service used by third parties, disable or
replace this provider with a source whose agreement explicitly covers that use.

### Twelve Data fallback

Create a Twelve Data key and set it before starting the application:

```powershell
$env:TWELVE_DATA_API_KEY = 'your-key'
.\dist\TradeChartLab-1.2.0-win64\tradingview_chart.exe
```

The key is read from the process environment and is never written to settings,
logs, source files, or Git. Only the active symbol is polled. Intraday charts
refresh every two minutes to stay within the free plan's daily request limit;
daily charts refresh every fifteen minutes.

The free plan may restrict display or redistribution. Review the current
[Twelve Data pricing and usage terms](https://twelvedata.com/pricing) before
using its data outside personal evaluation.

### Alpha Vantage research (optional)

Set a key before starting the application, then use the **Research** dock's
**Summary** tab and choose **Refresh Alpha Vantage research**:

```powershell
$env:ALPHA_VANTAGE_API_KEY = 'your-key'
.\dist\TradeChartLab-1.2.0-win64\tradingview_chart.exe
```

The refresh retrieves the selected company's overview followed by its earnings
calendar. It is deliberately manual to conserve provider quotas. Provider
availability, endpoint entitlement, request limits, and field coverage depend
on the account and can change. A failed calendar request keeps a valid company
overview and any previously loaded earnings dates, and reports the calendar
warning.

The provider's `AnalystTargetPrice` is labeled as an aggregate and is never
mixed into the locally recorded organization-level target calculation.
Organization summaries use only the latest dated target for each organization
within a currency; they never combine currencies. Earnings-calendar dates are
labeled **Estimated**, while every event retains its source and retrieval time.
The API key is read from the process environment and is not persisted.

### Offline behavior

Choose **File → Load offline demo** to avoid network requests. CSV data remains
local. If both online providers fail, the application shows an offline fallback
and retries later.

### Local historical cache

On desktop, each successful Yahoo or Twelve Data response is stored under the
application data directory in `history.sqlite`. The cache is used by the
Strategy Lab scanner; it is not a hidden data server and it does not make
provider data redistributable. A symbol becomes scannable after that
symbol/timeframe has been loaded at least once. The UI reports a symbol as
**Unavailable** when compatible cached bars do not exist or an indicator has
not completed warm-up.

Cache rows retain the provider identity and provider-supplied metadata. The app
does not merge bars from different providers into one series. Review each
provider's retention and usage terms before relying on the cache outside
personal evaluation.

### SEC CompanyFacts fundamentals

The desktop **Fundamental Lab** uses the official SEC ticker map and CompanyFacts
endpoints. Before choosing **Refresh SEC CompanyFacts**, set a contact-bearing
identity:

```powershell
$env:SEC_USER_AGENT = 'TradeChartLab personal research your-email@example.com'
```

Accepted facts are stored locally in a separate `fundamentals.sqlite` database
with taxonomy, tag, unit, reporting period, filing date, form, fiscal metadata,
accession, frame, source URL, and retrieval time. The SEC identity is read from
the process environment and is never saved. Initial population is manual to
respect SEC access policy; the app is not a bulk EDGAR downloader.

SEC CompanyFacts coverage varies by issuer and taxonomy. SEC data contains
reported facts, not analyst estimates, consensus targets, exchange quotes, or
recommendations. Unsupported concepts remain unavailable rather than being
guessed.

## Technical calculations

The indicator engine consumes the same validated bars shown on the chart. SMA,
EMA, rolling high/low, Volume SMA, and RSI accept periods from 1 to 500. VWAP
uses typical price `(high + low + close) / 3`, weighted by volume, and resets at
each UTC-day boundary. MACD exposes configurable fast, slow, and signal periods
and requires the slow period to be greater than the fast period.

Warm-up values are omitted until the required number of bars exists. All
calculations are descriptive views of historical input; they are not price
forecasts, trading recommendations, or guarantees of future results.

## Fundamental Lab assumptions

Only facts filed on or before the selected as-of date are eligible. The engine
keeps direct quarters, derives compatible cumulative Q2/Q3 values, derives Q4
from the annual value less the first three quarters, and sums four compatible
quarters for TTM. Reconstructed cells are marked and retain the latest
contributing filing date. Comparative/restated facts are matched using their
actual reporting windows rather than assuming the SEC fiscal-year field is a
calendar year.

Derived values use these definitions:

```text
free cash flow = TTM operating cash flow - abs(TTM capital expenditure)
long-term D/E  = latest mapped long-term debt / latest equity
ROE            = TTM net income / latest equity
market cap     = as-of raw daily close × diluted shares
```

ROE is therefore an ending-equity screening ratio, and “debt” is the mapped
long-term debt/lease concept rather than every possible liability. Valuation
multiples and DCF require a provider-attributed raw daily close on or before
the as-of date; load that symbol on the daily timeframe to seed the cache.
Absolute peer values are compared only within the same reported unit, with no
hidden FX conversion.

The screen operates on the symbols currently present in local watchlists and
with cached CompanyFacts. It is not a point-in-time constituent database and
does not remove survivorship bias from a historical universe. Screen alerts
only evaluate a current-date screen while the desktop process is running.

Event studies align a supplied earnings/filing date to the first UTC trading
date on or after it, or strictly after it when **After market close** is
selected. They use the prior close as the return baseline and require exact
benchmark dates for benchmark-relative results. “Abnormal return” is a
descriptive subtraction, not proof that the event caused the move.

## Risk & Context assumptions

The desktop **Risk & Context** dock uses completed raw daily bars from the
local provider-attributed cache. Load the security and the configurable
benchmark (default `SPY`) on **1D** at least once before analysis. Missing
benchmark history, SEC facts, or calendar coverage reduces the displayed
coverage instead of being interpreted as low risk.

The score is adverse evidence divided by available category weight. Its
categories are capped at 20 points for benchmark regime, 20 for
trend/momentum, 15 for latest-candle price action, 15 for
volatility/liquidity, 10 for price location, 10 for known events, and 10 for
point-in-time fundamentals. Constructive evidence is shown separately and does
not silently cancel a risk observation. The four labels—lower, moderate,
elevated, and high—describe measured conditions, not expected returns.

Historical comparisons use only information available on each setup date.
They require the same available-category mask and risk band within ten score
points, then report descriptive forward returns and close-to-close drawdowns.
Fewer than 30 matches is explicitly marked as a small sample and never called
a probability. Survivorship bias, provider retention, corporate-action
coverage, market microstructure, and future regime changes can still make
historical comparisons unrepresentative.

## Strategy Lab assumptions

The desktop Strategy Lab starts with a close/SMA crossing example. The reusable
rule engine supports open, high, low, close, volume, SMA, EMA, RSI, and volume
ratio operands; `>`, `<`, crosses-above, and crosses-below comparisons; and
field-to-field or field-to-constant rules.

Backtests use these conservative execution rules:

1. evaluate a signal only after bar `i` has completed;
2. execute it at bar `i + 1` open;
3. worsen entry and exit prices by the configured slippage;
4. charge commission on both sides; and
5. force-close a final open position at the last close so final equity and the
   trade list reconcile.

The editor supports up to 16 conditions in each all/any entry and exit group,
and the same definition drives backtests, scans, and strategy alerts. Holdout
mode preserves pre-split indicator warm-up but does not permit pre-split trades
to leak into the holdout result. The engine aligns optional cached
higher-timeframe values only after the source bar has closed and can derive
split/total-return Yahoo views without putting adjusted values into the raw
cache. It does not claim short selling, tax-lot optimization,
background/mobile alert delivery, or broker execution.

### Trading Simulator

Open **Tools → Trading Simulator** in the desktop application. Choose a
completed starting moment, enter the initial cash and execution assumptions,
then select one of these modes:

- **Automatic** evaluates the current strategy after every completed bar and
  queues an entry or exit for the following bar's open.
- **Manual** ignores strategy signals; Buy and Sell queue the user's action for
  the following open.
- **Assisted** shows the strategy proposal and requires Approve or Reject before
  it can be queued.

The simulator never fills on the decision bar. Slippage worsens each fill,
commission is charged on both sides, and account equity is marked at each
visible close. Unlike a completed backtest, stopping at the selected moment
does not force-close an open position; realized and unrealized results remain
separate. The decision audit explains every signal, ignored action, proposal,
fill, and rejection.

Resume data is local and includes a fingerprint of the exact symbol, provider,
timeframe, price basis, bar series, and strategy. If any source input changes,
the snapshot is rejected instead of replayed against different history. The
simulation is hypothetical, has no broker connection, and does not model
liquidity, partial fills, taxes, borrowing, or short sales.

### Safe Script Lab

Open **Tools → Safe Script Lab** to translate a small Pine-style strategy
subset into native Strategy Lab rules. The supported subset includes:

- `//@version=5` or `//@version=6` and one `strategy(...)` declaration;
- numeric `input.int` and `input.float` defaults;
- OHLCV operands plus `ta.sma(close, n)`, `ta.ema(close, n)`, and
  `ta.rsi(close, n)`;
- `ta.crossover`, `ta.crossunder`, `>`, and `<` conditions joined by one flat
  `and` or `or` group; and
- long `strategy.entry` plus `strategy.close`, using `if` or `when`.

The example selector includes three reviewed MIT-licensed adaptations: an
opmau SMA crossover, an Eterna EMA ribbon, and an Eterna-derived RSI
mean-reversion rule. Their pinned upstream originals, licenses, modification
notes, and compatibility policy are under `examples/pine/`. The original
scripts remain separate because they contain behavior the native engine must
reject; only the explicitly marked long-only adaptations are bundled into the
desktop application.

The importer can map initial capital, percent-of-equity allocation, and fixed
cash-per-order commission only when their meaning matches the native engine.
It rejects short positions, history indexing, functions/loops, mutable state,
`request.*`, `strategy.exit`/`strategy.order`, stop/limit orders, tick
calculations, pyramiding above one, unsupported sizing, and nested mixed
Boolean groups. Size, line, symbol, and condition limits bound the parser.

This is a compatibility importer, not a Pine runtime: it does not execute
scripts, render Pine plots, or promise identical TradingView results. Review
the native preview and diagnostics before applying. Imported text stays in
local application settings. Users are responsible for permission to copy or
redistribute third-party scripts and for complying with their licenses.

## Portfolio, alerts, and comparison assumptions

The paper ledger is long-only and uses one base currency per portfolio. Buys
increase an average-cost position; sells reduce that position and recognize the
difference between sale proceeds and average cost. Cash flows, dividends, fees,
and splits are explicit transactions. It is not a broker statement or a
jurisdiction-specific tax-lot calculator. A portfolio remains marked
**incomplete** until every open holding has a current non-synthetic quote.

Portfolio risk uses completed raw daily cached bars with at least 20 common
returns and the portfolio's current weights; it does not reconstruct historical
weights. Sharpe uses a zero-percent risk-free rate. Historical VaR/CVaR is
descriptive, XIRR requires the ending valuation to follow every external cash
flow, and daily-close TWR is labeled approximate. Load each holding and the
benchmark on the daily timeframe once to seed compatible cache history.
Rebalance rows are calculations only and never create orders or transactions.

Saved alerts and their audit history are local. Price/indicator/strategy alerts
evaluate completed bars while the desktop process is running. A system-tray
message is a foreground convenience, not a durable server or mobile push
service. Event reminders preserve the underlying research-event source and do
not convert estimated dates into confirmed dates.

Comparison statistics use only exact common timestamps after excluding forming
bars. Both close series are normalized to 100 at the first common bar; relative
return is the ratio of those normalized series. Correlation is Pearson
correlation of close-to-close log returns and is unavailable when either return
series has zero variance. It is descriptive, sensitive to timeframe and sample
window, and not a prediction.

## Research and margin assumptions

Organization target records require a symbol, organization, positive target,
currency, and publication date. A source URL and rating are optional. The
application calculates mean, median, and range per currency from the latest
record for each organization. It does not manufacture analyst estimates.

The margin panel models a long-only account with one uniform maintenance rate:

```text
equity = long market value + other equity - margin debit
requirement = long market value × maintenance rate
call threshold = (margin debit - other equity) / (1 - maintenance rate)
```

Real brokers can apply security-specific and concentrated-position house
requirements, change requirements without a calendar event, and liquidate
under their own agreements. Therefore the panel is a transparent stress
scenario, not a broker feed, legal notice, or prediction of a “margin-call
day.”

SEC EDGAR and FRED are not analyst-target sources. The manual EDGAR refresh maps
the selected ticker to an SEC CIK and imports recent issuer filings. Set a
contact-bearing identity before using filing or CompanyFacts refresh:

```powershell
$env:SEC_USER_AGENT = 'TradeChartLab personal research your-email@example.com'
```

The manual FRED refresh imports selected major macro release dates over the next
90 days:

```powershell
$env:FRED_API_KEY = '0123456789abcdef0123456789abcdef'
```

Keys and the SEC identity are read from the process environment and are not
stored in application settings. These calendars are informational and can be
revised by their source.

## Watchlist CSV format

Use **File → Export active watchlist CSV** to create the canonical format:

```text
symbol,note
"AAPL","Large, liquid"
"BRK-B","Review after earnings"
```

Import merges valid, previously absent symbols into the active named list.
Symbols are normalized to uppercase. Duplicate or malformed rows are reported
and skipped. Watchlists and their notes are stored locally with `QSettings`;
they are not transmitted to Yahoo or Twelve Data.

## Organization target CSV format

The Research Targets tab imports and exports:

```text
symbol,organization,target,currency,published_date,rating,source_url
"AAPL","Example Research",250,USD,2026-07-20,"Buy","https://example.com/report"
```

Invalid rows are skipped and reported. Re-importing the same
symbol/organization/currency/date is treated as a duplicate. Text exported to
watchlist and research CSV files is neutralized when it could otherwise be
interpreted as a spreadsheet formula.

## Data boundary

Lightweight Charts accepts application-supplied arrays through methods such as
`setData()` and `update()`. It does not download exchange data or grant market
data rights. This repository deliberately separates:

1. `Bar` domain values and validation;
2. online acquisition and provider-specific JSON parsing;
3. offline sources (`DemoDataSource` and `CsvBarLoader`);
4. provider-independent technical calculations and summary statistics;
5. provider-attributed SQLite historical storage;
6. reusable strategy rules, backtesting, replay, scanning, and alert logic;
7. local portfolio accounting and valuation-state contracts;
8. research/event/target contracts and the margin scenario engine;
9. optional research acquisition and provider-specific parsers;
10. point-in-time SEC fact acquisition, storage, analytics, screening, and
    event impact;
11. exact-timestamp comparison analysis;
12. the Qt/WebChannel bridge; and
13. the JavaScript chart renderer.

## Windows prerequisites

- Windows 10/11
- Visual Studio 2022 C++ workload
- CMake 3.24+
- Qt 6.8.3 with `qtwebengine` and `qtwebchannel`
- Node.js 22+ only when refreshing the vendored chart bundle

The repository includes a bootstrap script that installs the required Qt kit
locally with `aqtinstall`.

## Build

```powershell
pwsh -NoProfile -File .\scripts\bootstrap-qt.ps1

$qtRoot = (Resolve-Path .\.qt\6.8.3\msvc2022_64).Path
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

& $cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 -DCMAKE_PREFIX_PATH="$qtRoot"
& $cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run:

```powershell
$env:PATH = "$qtRoot\bin;$env:PATH"
.\build\Release\tradingview_chart.exe
```

The raw build EXE requires Qt on `PATH`. For a self-contained application, use
the deployable folder/ZIP created by `package-windows.ps1`; do not copy the EXE
by itself.

WebEngine smoke check:

```powershell
$env:QTWEBENGINE_CHROMIUM_FLAGS = '--disable-gpu'
.\build\Release\tradingview_chart.exe --smoke-test
```

Create a tested, deployable Windows ZIP:

```powershell
pwsh -NoProfile -File .\scripts\package-windows.ps1
```

The Windows GitHub Actions workflow runs this same packaging path and uploads
the tested ZIP plus its SHA-256 file as workflow artifacts. This verifies the
deployed Qt WebChannel, WebEngine, networking, platform, and TLS runtime files,
not only the raw build output.

## Android APK

The release page provides
`TradeChartLab-1.2.0-android-arm64-v8a.apk` for typical modern Android
phones. Download it on the phone, verify its SHA-256 against the adjacent
`.sha256` file, allow installation from the browser or file manager when
Android prompts, and open **TradeChart Lab**. Android may show a Play Protect
warning because this APK is distributed directly rather than through Google
Play.

Requirements:

- Android 9 or newer;
- an ARM64 (`arm64-v8a`) device; and
- internet access for live Yahoo or Twelve Data values.

The app starts by requesting AAPL from Yahoo. If the request fails, it displays
deterministic demo data. A Twelve Data key can be entered under **Provider
fallback** for the current process only; it is cleared when the app exits.
Portrait mode exposes the compact symbol, timeframe, style, indicator, and
provider controls. Landscape mode automatically hides that panel to preserve
the chart area; rotate back to portrait to change settings. Market-structure
overlays are opt-in on Android so their labels do not obscure a small chart.

To build the APK locally, install Android Studio command-line tools/JDK 17+,
then run:

```powershell
pwsh -NoProfile -File .\scripts\bootstrap-android.ps1
pwsh -NoProfile -File .\scripts\package-android.ps1
```

The packaging script requires the established signing key under
`%LOCALAPPDATA%\TradingViewChart\signing`, signs and verifies the release APK,
and writes the APK plus checksum to `dist`. Back up that signing directory
securely: Android upgrades must be signed by the same key. Missing or partial
signing material is an error and never silently creates a replacement key.

Only for a brand-new application identity with no previous installs, explicitly
initialize signing material once:

```powershell
pwsh -NoProfile -File .\scripts\package-android.ps1 -InitializeSigningKey
```

For CI or a disposable unsigned build:

```powershell
pwsh -NoProfile -File .\scripts\package-android.ps1 -SkipSigning
```

Unsigned and debug APKs have `-unsigned` and `-debug` suffixes, so they cannot
overwrite the canonical signed release artifact.

## CSV format

The first row must contain these case-insensitive column names:

```text
timestamp,open,high,low,close,volume
```

`timestamp` accepts Unix seconds, Unix milliseconds, or an ISO-8601 value such
as `2026-07-24T09:30:00Z`. Rows must be strictly increasing. Prices must be
finite and positive, volume must be non-negative, and each high/low must contain
its open and close.

## Refreshing Lightweight Charts

The chart-rendering bundle is pinned in `package.json` and committed under
`assets/web/vendor`, so no CDN is required at runtime.

```powershell
npm ci
npm run sync-web-deps
```

Review upstream release notes and rerun all tests before changing the pinned
version.

## Licensing

Application code is MIT licensed. TradingView Lightweight Charts 5.2.0 is
Apache-2.0 licensed and carries additional attribution requirements. The
application keeps the built-in TradingView attribution logo enabled and exposes
an About dialog linking to TradingView. See `THIRD_PARTY_NOTICES.md` and the
vendored upstream license files.
