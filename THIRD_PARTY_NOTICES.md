# Third-party notices

## TradingView Lightweight Charts 5.2.0

Source: <https://github.com/tradingview/lightweight-charts>

License: Apache License 2.0. A copy is stored at
`assets/web/vendor/LICENSE.lightweight-charts`.

Required upstream notice:

> TradingView Lightweight Charts™ Copyright (с) 2025 TradingView, Inc. https://www.tradingview.com/

The built-in `attributionLogo` option remains enabled. The application's About
dialog also links to <https://www.tradingview.com/>.

Lightweight Charts incorporates portions of `tslib`, copyright Microsoft
Corporation, under the BSD Zero Clause License. The upstream distribution
includes the applicable notice.

## Bundled open-source Pine-style examples

The desktop Safe Script Lab includes modified, long-only examples derived from:

- `opmau/TradingView`, copyright (c) 2024 opmau; and
- `EternaHybridExchange/tradingview-strategies`, copyright (c) 2026 Eterna
  Hybrid Exchange.

Both sources are used under the MIT License. The pinned upstream commits,
original source snapshots, exact modification policy, and provenance links are
documented in `examples/pine/README.md`. Complete license copies are stored at
`examples/pine/licenses/opmau-MIT.txt` and
`examples/pine/licenses/eterna-MIT.txt` and are shipped with the Windows
package.

The adaptations remove behavior that TradeChart Lab cannot reproduce exactly,
including shorts, leverage, webhooks, percent commission, and unsupported
order semantics. They are educational compatibility fixtures, not
endorsements or trading recommendations.
