# Open-source Pine-style examples

This directory contains pinned upstream source snapshots and reviewed
long-only adaptations for TradeChart Lab's Safe Script Lab.

Only sources with an explicit permissive license are included. A public or
"open" TradingView script without a redistribution license is not enough.

## Sources

| Repository | Pinned commit | License | Included upstream source |
|---|---|---|---|
| [opmau/TradingView](https://github.com/opmau/TradingView) | `aca6c98eff5c1f48aab8de6a9fcc6513f0415e40` | MIT | `strategies/sma_crossover_strategy.pine` |
| [EternaHybridExchange/tradingview-strategies](https://github.com/EternaHybridExchange/tradingview-strategies) | `882564096838e7a449d4666dd9f8771891b293ae` | MIT | `ema-ribbon/strategy.pine`, `momentum_combo.pine` |

The complete corresponding MIT license texts are under `licenses/`.

Pinned upstream SHA-256 values:

| Local snapshot | SHA-256 |
|---|---|
| `upstream/opmau_sma_crossover_strategy.pine` | `9564a9a924fb5b90879d438a789b2676945a04655137642f56fbf86ce8cf49ff` |
| `upstream/eterna_ema_ribbon_strategy.pine` | `f4220dbbe009bf84c3a3bcc1ad8c382995434a9908e8edf184efc4effcbefd33` |
| `upstream/eterna_momentum_combo_strategy.pine` | `41b4a8632f1333df3dae44df4ad00ee7634d73d8f676f60fe022a4ce037c3b4e` |

## Directory policy

- `upstream/` preserves the downloaded source at the pinned commit. These
  files are research fixtures and are not silently changed to fit the app.
- `compatible/` contains clearly marked modifications that compile into
  TradeChart Lab's native completed-bar, next-open, long-only rule model.
- Compatible examples never add broker/webhook behavior and never approximate
  shorts, stop/limit orders, leverage, or unsupported commission types.
- Automated tests require every file under `compatible/` to compile and every
  file under `upstream/` to be rejected by the current subset. If the importer
  grows, the upstream expectations must be reviewed deliberately.

These examples are educational test inputs, not recommendations. Historical
performance depends on symbol, timeframe, data quality, costs, and the chosen
test window and does not predict future results.
