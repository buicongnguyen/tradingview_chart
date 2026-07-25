# TradingView Chart

An online-capable C++20/Qt 6 desktop chart viewer powered by
[TradingView Lightweight Charts](https://github.com/tradingview/lightweight-charts).

The application does **not** require Alpaca, a broker account, or an application
backend. It requests the active symbol from Yahoo Finance first. If that request
fails and `TWELVE_DATA_API_KEY` is configured, it uses Twelve Data as a
fallback. When neither provider is available, it displays deterministic offline
demo data. Local OHLCV CSV import remains available.

Lightweight Charts is only the renderer. Provider availability, freshness,
rate limits, terms, and market-data display rights still apply.

## MVP features

- Qt Widgets desktop shell with an embedded, local Qt WebEngine chart.
- Candlestick, line, and area display modes.
- Volume overlay, crosshair, pan, zoom, fit-to-data, and light/dark themes.
- Yahoo Finance chart data as the default online source.
- Optional Twelve Data REST fallback configured through an environment variable.
- Deterministic offline demo data for a small watchlist and five timeframes.
- Local CSV import with validation and clear error messages.
- Settings persistence for window geometry, chart mode, theme, symbol, and
  timeframe.
- No runtime CDN, broker connection, or application backend.
- C++ unit tests plus a Qt WebEngine startup smoke mode.

## Market-data providers

### Yahoo Finance

Yahoo Finance is the default because its chart endpoint does not require a key.
The endpoint is unofficial and unsupported, so it can be rate-limited, changed,
or withdrawn without notice. The application labels Yahoo data explicitly and
falls back safely when a request fails.

### Twelve Data fallback

Create a Twelve Data key and set it before starting the application:

```powershell
$env:TWELVE_DATA_API_KEY = 'your-key'
.\dist\TradingViewChart-0.1.0-win64\tradingview_chart.exe
```

The key is read from the process environment and is never written to settings,
logs, source files, or Git. Only the active symbol is polled. Intraday charts
refresh every two minutes to stay within the free plan's daily request limit;
daily charts refresh every fifteen minutes.

The free plan may restrict display or redistribution. Review the current
[Twelve Data pricing and usage terms](https://twelvedata.com/pricing) before
using its data outside personal evaluation.

### Offline behavior

Choose **File → Load offline demo** to avoid network requests. CSV data remains
local. If both online providers fail, the application shows an offline fallback
and retries later.

## Data boundary

Lightweight Charts accepts application-supplied arrays through methods such as
`setData()` and `update()`. It does not download exchange data or grant market
data rights. This repository deliberately separates:

1. `Bar` domain values and validation;
2. online acquisition and provider-specific JSON parsing;
3. offline sources (`DemoDataSource` and `CsvBarLoader`);
4. the Qt/WebChannel bridge; and
5. the JavaScript chart renderer.

## Prerequisites

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
