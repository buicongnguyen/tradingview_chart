# TradingView Chart

An online-capable C++20/Qt 6 chart viewer for Windows and Android, powered by
[TradingView Lightweight Charts](https://github.com/tradingview/lightweight-charts).

The application does **not** require Alpaca, a broker account, or an application
backend. It requests the active symbol from Yahoo Finance first. If that request
fails and `TWELVE_DATA_API_KEY` is configured, it uses Twelve Data as a
fallback. When neither provider is available, it displays deterministic offline
demo data. Local OHLCV CSV import remains available.

Lightweight Charts is only the renderer. Provider availability, freshness,
rate limits, terms, and market-data display rights still apply.

## Version 0.5 features

- A desktop **Strategy Lab** with an editable entry rule and exit rule. The
  underlying model supports multiple conditions and is shared by every
  strategy feature.
- Long-only historical backtests that evaluate completed bars and execute at
  the next bar open, with configurable allocation, fixed commissions,
  slippage, fractional shares, trade reconciliation, maximum drawdown, win
  rate, profit factor, and exposure.
- Deterministic bar replay with start depth, step, play/pause, and speed
  controls.
- A cached-watchlist scanner and foreground alerts using the exact same rule
  evaluator. Alerts run only while the desktop application is open.
- A local SQLite historical cache for successfully validated polled data.
  Overlapping downloads are transactionally upserted without deleting older
  non-overlapping bars, and each series retains provider, market metadata,
  retrieval time, and cache time. Synthetic demo data is never cached.
- A provider candle whose interval is still forming remains visible on the
  chart but is excluded from cache, replay, backtests, scans, and alerts until
  its interval has completed.
- Bounded network/CSV reads, formula-safe spreadsheet exports, bounded research
  targets, and Android packaging that cannot silently replace the established
  signing identity or overwrite a signed APK with an unsigned build.

- Native Qt Android application for Android 9 (API 28) and newer, distributed
  as a signed ARM64 APK.
- Responsive Qt Quick controls surrounding the same locally packaged
  Lightweight Charts renderer used by the desktop application.
- Android-native HTTPS through `HttpsURLConnection`, the device trust store,
  and TLS-only URLs; no bundled OpenSSL binary or cleartext traffic.
- Live Yahoo Finance data by default, plus an optional in-session Twelve Data
  key that is not persisted.
- Candlestick, line, and area charts with volume and the desktop calculation
  engine's technical indicators on mobile.
- Android CI that builds and inspects an unsigned reproducible APK on every
  pull request; signed public APKs are attached to GitHub Releases.

- Qt Widgets desktop shell with an embedded, local Qt WebEngine chart.
- Candlestick, line, and area display modes.
- Linear, logarithmic, and percentage price scales.
- Volume overlay, pan, zoom, fit-to-data, and light/dark themes.
- A crosshair information strip with UTC timestamp, OHLCV, candle change,
  candle range, volume relative to its 20-bar average, and indicator values.
- Several simultaneous, configurable local indicators: SMA, EMA,
  UTC-session VWAP, RSI, MACD, rolling high, rolling low, and Volume SMA.
  Oscillators receive independent panes and warm-up remains explicit.
- A calculated-information panel with latest close, last-bar change, loaded
  high/low range, range position, average volume (20), and all enabled indicator
  values.
- A data-status panel that distinguishes polled REST data, local CSV data, and
  synthetic demo data; it also displays provider-reported delay, retrieval time,
  last candle age, market metadata, and bar count.
- Persistent named watchlists with add/remove, local notes, manual/ticker sort,
  reorder, and CSV import/export.
- A provenance-aware research workspace with an event calendar, provider
  company overview, provider aggregate target/rating counts, and separately
  calculated organization-level target summaries.
- Manual organization-target and event entry with local persistence; target
  CSV import/export retains organization, date, currency, rating, and source
  URL.
- Optional, explicit Alpha Vantage overview and earnings-calendar refresh
  through `ALPHA_VANTAGE_API_KEY`. Research requests are never made
  automatically.
- A long-only margin-maintenance scenario calculator showing current and
  stressed equity, requirement, cushion, and the calculated market-value
  threshold. It does not claim to predict a margin-call date.
- Yahoo Finance chart data as the default online source.
- Optional Twelve Data REST fallback configured through an environment variable.
- Deterministic offline demo data for a small watchlist and five timeframes.
- Local CSV import with validation and clear error messages.
- Settings persistence for window geometry, chart mode, theme, price scale,
  symbol, timeframe, indicator parameters, named watchlists, research records,
  and margin scenario assumptions.
- No runtime CDN, broker connection, or application backend.
- C++ unit tests plus a Qt WebEngine startup smoke mode.

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
.\dist\TradingViewChart-0.5.0-win64\tradingview_chart.exe
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
.\dist\TradingViewChart-0.5.0-win64\tradingview_chart.exe
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

## Technical calculations

The indicator engine consumes the same validated bars shown on the chart. SMA,
EMA, rolling high/low, Volume SMA, and RSI accept periods from 1 to 500. VWAP
uses typical price `(high + low + close) / 3`, weighted by volume, and resets at
each UTC-day boundary. MACD exposes configurable fast, slow, and signal periods
and requires the slow period to be greater than the fast period.

Warm-up values are omitted until the required number of bars exists. All
calculations are descriptive views of historical input; they are not price
forecasts, trading recommendations, or guarantees of future results.

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

The first UI edits one entry condition and one exit condition. The saved model
and engine support multiple all/any conditions for future editors. Version 0.5
does not claim short selling, portfolio or multi-timeframe backtests,
background/mobile notifications, dividend-adjusted performance, or broker
execution.

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

SEC EDGAR and FRED are not treated as analyst-target sources. A future
integration can use EDGAR for issuer filings and FRED for macroeconomic release
data once issuer mapping, series selection, release calendars, provenance, and
provider quotas are modeled explicitly.

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
7. research/event/target contracts and the margin scenario engine;
8. optional research acquisition and provider-specific parsers;
9. the Qt/WebChannel bridge; and
10. the JavaScript chart renderer.

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
`TradingViewChart-0.5.0-android-arm64-v8a.apk` for typical modern Android
phones. Download it on the phone, verify its SHA-256 against the adjacent
`.sha256` file, allow installation from the browser or file manager when
Android prompts, and open **TradingView Chart**. Android may show a Play Protect
warning because this APK is distributed directly rather than through Google
Play.

Requirements:

- Android 9 or newer;
- an ARM64 (`arm64-v8a`) device; and
- internet access for live Yahoo or Twelve Data values.

The app starts by requesting AAPL from Yahoo. If the request fails, it displays
deterministic demo data. A Twelve Data key can be entered under **Provider
fallback** for the current process only; it is cleared when the app exits.

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
