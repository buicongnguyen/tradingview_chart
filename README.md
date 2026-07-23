# TradingView Chart

An offline-first C++20/Qt 6 desktop chart viewer powered by
[TradingView Lightweight Charts](https://github.com/tradingview/lightweight-charts).

The application does **not** require Alpaca, a broker account, API keys, or an
application backend. It starts with deterministic demo data and can open local
OHLCV CSV files. Lightweight Charts is the renderer; it does not include a
market-data feed. Current or live quotes would still require a separately
licensed provider adapter.

## MVP features

- Qt Widgets desktop shell with an embedded, local Qt WebEngine chart.
- Candlestick, line, and area display modes.
- Volume overlay, crosshair, pan, zoom, fit-to-data, and light/dark themes.
- Deterministic offline demo data for a small watchlist and five timeframes.
- Local CSV import with validation and clear error messages.
- Settings persistence for window geometry, chart mode, theme, symbol, and
  timeframe.
- No runtime CDN, broker, API key, or network dependency.
- C++ unit tests plus a Qt WebEngine startup smoke mode.

## Data boundary

Lightweight Charts accepts application-supplied arrays through methods such as
`setData()` and `update()`. It does not download exchange data or grant market
data rights. This repository deliberately separates:

1. `Bar` domain values and validation;
2. offline sources (`DemoDataSource` and `CsvBarLoader`);
3. the Qt/WebChannel bridge; and
4. the JavaScript chart renderer.

Future HTTP/WebSocket providers can implement a provider interface without
changing the renderer. They remain optional and must document credentials,
rate limits, redistribution, and display rights.

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
.\build\Release\tradingview_chart.exe
```

WebEngine smoke check:

```powershell
$env:QTWEBENGINE_CHROMIUM_FLAGS = '--disable-gpu'
.\build\Release\tradingview_chart.exe --smoke-test
```

Create a tested, deployable Windows ZIP:

```powershell
pwsh -NoProfile -File .\scripts\package-windows.ps1
```

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

The release bundle is pinned in `package.json` and committed under
`assets/web/vendor` so the application stays offline at runtime.

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
