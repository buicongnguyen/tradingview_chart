(() => {
  "use strict";

  const container = document.getElementById("chart");
  const loading = document.getElementById("loading");
  const symbolLabel = document.getElementById("symbol");
  const timeframeLabel = document.getElementById("timeframe");
  const indicatorLabel = document.getElementById("indicator");
  const sourceLabel = document.getElementById("source");
  const crosshairDetails = document.getElementById("crosshair-details");

  let bridge = null;
  let dark = true;
  let style = "candlestick";
  let priceScaleMode = "normal";
  let bars = [];
  let priceSeries = null;
  let indicatorSeries = [];
  let indicatorCalculations = [];
  let barIndexByTime = new Map();

  const colors = () => dark
    ? {
        background: "#131722",
        text: "#d1d4dc",
        grid: "#242733",
        border: "#2a2e39",
        up: "#26a69a",
        down: "#ef5350",
        line: "#2962ff",
        areaTop: "rgba(41, 98, 255, 0.45)",
        areaBottom: "rgba(41, 98, 255, 0.04)",
      }
    : {
        background: "#ffffff",
        text: "#131722",
        grid: "#eceff3",
        border: "#d1d4dc",
        up: "#089981",
        down: "#f23645",
        line: "#2962ff",
        areaTop: "rgba(41, 98, 255, 0.32)",
        areaBottom: "rgba(41, 98, 255, 0.03)",
      };

  const chart = LightweightCharts.createChart(container, {
    autoSize: true,
    layout: {
      attributionLogo: true,
      background: {
        type: LightweightCharts.ColorType.Solid,
        color: colors().background,
      },
      textColor: colors().text,
      fontFamily: 'Inter, "Segoe UI", system-ui, sans-serif',
    },
    grid: {
      vertLines: { color: colors().grid },
      horzLines: { color: colors().grid },
    },
    crosshair: {
      mode: LightweightCharts.CrosshairMode.Magnet,
    },
    rightPriceScale: {
      borderColor: colors().border,
    },
    timeScale: {
      borderColor: colors().border,
      rightOffset: 6,
      timeVisible: true,
      secondsVisible: false,
    },
    handleScroll: {
      mouseWheel: true,
      pressedMouseMove: true,
      horzTouchDrag: true,
      vertTouchDrag: true,
    },
    handleScale: {
      axisPressedMouseMove: true,
      mouseWheel: true,
      pinch: true,
    },
  });

  const volumeSeries = chart.addSeries(LightweightCharts.HistogramSeries, {
    priceFormat: { type: "volume" },
    priceScaleId: "volume",
    lastValueVisible: false,
    priceLineVisible: false,
  });
  chart.priceScale("volume").applyOptions({
    scaleMargins: {
      top: 0.8,
      bottom: 0,
    },
  });

  function createPriceSeries() {
    const palette = colors();
    if (priceSeries !== null) {
      chart.removeSeries(priceSeries);
    }

    if (style === "line") {
      priceSeries = chart.addSeries(LightweightCharts.LineSeries, {
        color: palette.line,
        lineWidth: 2,
      });
    } else if (style === "area") {
      priceSeries = chart.addSeries(LightweightCharts.AreaSeries, {
        lineColor: palette.line,
        lineWidth: 2,
        topColor: palette.areaTop,
        bottomColor: palette.areaBottom,
      });
    } else {
      priceSeries = chart.addSeries(LightweightCharts.CandlestickSeries, {
        upColor: palette.up,
        downColor: palette.down,
        wickUpColor: palette.up,
        wickDownColor: palette.down,
        borderVisible: false,
      });
    }
    applyPriceScaleMode();
    setSeriesData();
  }

  function applyPriceScaleMode() {
    if (priceSeries === null) {
      return;
    }
    const modes = {
      normal: LightweightCharts.PriceScaleMode.Normal,
      logarithmic: LightweightCharts.PriceScaleMode.Logarithmic,
      percentage: LightweightCharts.PriceScaleMode.Percentage,
    };
    priceSeries.priceScale().applyOptions({ mode: modes[priceScaleMode] });
  }

  function setPriceScaleMode(value) {
    const nextMode = String(value);
    if (!["normal", "logarithmic", "percentage"].includes(nextMode)) {
      throw new Error(`Unsupported price scale mode: ${nextMode}`);
    }
    priceScaleMode = nextMode;
    applyPriceScaleMode();
  }

  function normalizeBar(bar) {
    return {
      time: Number(bar.time),
      open: Number(bar.open),
      high: Number(bar.high),
      low: Number(bar.low),
      close: Number(bar.close),
      volume: Number(bar.volume),
    };
  }

  function setSeriesData() {
    if (priceSeries === null) {
      return;
    }
    if (style === "candlestick") {
      priceSeries.setData(bars.map(({ time, open, high, low, close }) => ({
        time,
        open,
        high,
        low,
        close,
      })));
    } else {
      priceSeries.setData(bars.map(({ time, close }) => ({
        time,
        value: close,
      })));
    }

    const palette = colors();
    volumeSeries.setData(bars.map(({ time, open, close, volume }) => ({
      time,
      value: volume,
      color: close >= open
        ? `${palette.up}99`
        : `${palette.down}99`,
    })));
  }

  function setTheme(value) {
    const nextDark = Boolean(value);
    if (nextDark === dark && priceSeries !== null) {
      return;
    }
    dark = nextDark;
    const palette = colors();
    document.body.classList.toggle("dark", dark);
    document.body.classList.toggle("light", !dark);
    chart.applyOptions({
      layout: {
        attributionLogo: true,
        background: {
          type: LightweightCharts.ColorType.Solid,
          color: palette.background,
        },
        textColor: palette.text,
      },
      grid: {
        vertLines: { color: palette.grid },
        horzLines: { color: palette.grid },
      },
      rightPriceScale: { borderColor: palette.border },
      timeScale: { borderColor: palette.border },
    });
    createPriceSeries();
  }

  function setStyle(value) {
    const nextStyle = String(value);
    if (!["candlestick", "line", "area"].includes(nextStyle)) {
      throw new Error(`Unsupported chart style: ${nextStyle}`);
    }
    if (nextStyle === style && priceSeries !== null) {
      return;
    }
    style = nextStyle;
    createPriceSeries();
  }

  function clearIndicatorSeries() {
    for (const series of indicatorSeries) {
      chart.removeSeries(series);
    }
    indicatorSeries = [];

    const panes = chart.panes();
    for (let index = panes.length - 1; index >= 1; --index) {
      if (panes[index].getSeries().length === 0) {
        chart.removePane(index);
      }
    }
  }

  function indicatorPoints(values) {
    return Array.from(values ?? [], (point) => ({
      time: Number(point.time),
      value: Number(point.value),
    }));
  }

  function calculationPoints(values) {
    return indicatorPoints(values);
  }

  function setIndicators(values) {
    clearIndicatorSeries();
    indicatorCalculations = Array.from(values ?? [], (calculation) => ({
      kind: String(calculation?.kind ?? "none"),
      label: String(calculation?.label ?? "None"),
      primary: calculationPoints(calculation?.primary),
      secondary: calculationPoints(calculation?.secondary),
      histogram: calculationPoints(calculation?.histogram),
    }));

    indicatorLabel.textContent = indicatorCalculations.length === 0
      ? "No indicators"
      : indicatorCalculations.map((calculation) =>
          calculation.primary.length === 0
            ? `${calculation.label} · warming`
            : calculation.label).join(" · ");

    let nextPane = 1;
    for (const calculation of indicatorCalculations) {
      const { kind, primary, secondary, histogram } = calculation;
      if (kind === "none" || primary.length === 0) {
        continue;
      }

      if (["sma", "ema", "vwap", "rolling-high", "rolling-low"].includes(kind)) {
        const colorsByKind = {
          sma: "#ff9800",
          ema: "#ab47bc",
          vwap: "#00acc1",
          "rolling-high": "#ef5350",
          "rolling-low": "#26a69a",
        };
        const overlay = chart.addSeries(LightweightCharts.LineSeries, {
          color: colorsByKind[kind],
          lineWidth: kind.startsWith("rolling-") ? 1 : 2,
          lineStyle: kind.startsWith("rolling-")
            ? LightweightCharts.LineStyle.Dashed
            : LightweightCharts.LineStyle.Solid,
          lastValueVisible: true,
          priceLineVisible: false,
          crosshairMarkerVisible: true,
        });
        overlay.setData(primary);
        indicatorSeries.push(overlay);
        continue;
      }

      if (kind === "volume-sma") {
        const volumeAverage = chart.addSeries(LightweightCharts.LineSeries, {
          color: "#90a4ae",
          lineWidth: 2,
          lastValueVisible: false,
          priceLineVisible: false,
          priceScaleId: "volume",
        });
        volumeAverage.setData(primary);
        indicatorSeries.push(volumeAverage);
        continue;
      }

      const pane = nextPane++;
      if (kind === "rsi") {
        const rsi = chart.addSeries(LightweightCharts.LineSeries, {
          color: "#7e57c2",
          lineWidth: 2,
          lastValueVisible: true,
          priceLineVisible: false,
        }, pane);
        rsi.setData(primary);
        rsi.createPriceLine({
          price: 70,
          color: "#ef535088",
          lineStyle: LightweightCharts.LineStyle.Dashed,
          lineWidth: 1,
          axisLabelVisible: true,
          title: "70",
        });
        rsi.createPriceLine({
          price: 30,
          color: "#26a69a88",
          lineStyle: LightweightCharts.LineStyle.Dashed,
          lineWidth: 1,
          axisLabelVisible: true,
          title: "30",
        });
        indicatorSeries.push(rsi);
      } else if (kind === "macd") {
        const histogramSeries = chart.addSeries(
          LightweightCharts.HistogramSeries,
          {
            base: 0,
            lastValueVisible: false,
            priceLineVisible: false,
          },
          pane,
        );
        histogramSeries.setData(histogram.map((point) => ({
          ...point,
          color: point.value >= 0 ? "#26a69a99" : "#ef535099",
        })));
        histogramSeries.createPriceLine({
          price: 0,
          color: "#787b8688",
          lineStyle: LightweightCharts.LineStyle.Dashed,
          lineWidth: 1,
          axisLabelVisible: false,
        });

        const macd = chart.addSeries(LightweightCharts.LineSeries, {
          color: "#ffc107",
          lineWidth: 2,
          lastValueVisible: true,
          priceLineVisible: false,
        }, pane);
        macd.setData(primary);
        const signal = chart.addSeries(LightweightCharts.LineSeries, {
          color: "#42a5f5",
          lineWidth: 2,
          lastValueVisible: true,
          priceLineVisible: false,
        }, pane);
        signal.setData(secondary);
        indicatorSeries.push(histogramSeries, macd, signal);
      }
    }

    const panes = chart.panes();
    if (panes.length > 1) {
      panes[0].setStretchFactor(4);
      for (let index = 1; index < panes.length; ++index) {
        panes[index].setStretchFactor(1);
      }
    }
    updateCrosshairDetails(bars.at(-1)?.time);
  }

  const formatPrice = (value) => {
    const number = Number(value);
    const decimals = Math.abs(number) < 1 ? 4 : 2;
    return Number.isFinite(number)
      ? number.toLocaleString(undefined, {
          minimumFractionDigits: decimals,
          maximumFractionDigits: decimals,
        })
      : "—";
  };

  const formatVolume = (value) => Number(value).toLocaleString(undefined, {
    maximumFractionDigits: 0,
  });

  function pointValue(points, time) {
    const point = points.find((candidate) => candidate.time === time);
    return point?.value;
  }

  function updateCrosshairDetails(time) {
    const index = barIndexByTime.get(Number(time));
    if (index === undefined) {
      crosshairDetails.textContent =
        "Move the crosshair over a candle to inspect OHLCV and calculations.";
      return;
    }
    const bar = bars[index];
    const previousClose = index > 0 ? bars[index - 1].close : bar.open;
    const change = bar.close - previousClose;
    const changePercent = previousClose > 0
      ? change * 100 / previousClose
      : 0;
    const firstVolumeIndex = Math.max(0, index - 19);
    const volumeWindow = bars.slice(firstVolumeIndex, index + 1);
    const averageVolume = volumeWindow.reduce(
      (total, candidate) => total + candidate.volume,
      0,
    ) / volumeWindow.length;
    const volumeRatio = averageVolume > 0 ? bar.volume / averageVolume : 0;
    const date = new Date(bar.time * 1000);
    const details = [
      date.toISOString().replace(".000Z", "Z"),
      `O ${formatPrice(bar.open)}`,
      `H ${formatPrice(bar.high)}`,
      `L ${formatPrice(bar.low)}`,
      `C ${formatPrice(bar.close)}`,
      `Δ ${change >= 0 ? "+" : ""}${formatPrice(change)} (${changePercent >= 0 ? "+" : ""}${changePercent.toFixed(2)}%)`,
      `Range ${formatPrice(bar.high - bar.low)}`,
      `Vol ${formatVolume(bar.volume)} (${volumeRatio.toFixed(2)}× avg20)`,
    ];
    for (const calculation of indicatorCalculations) {
      const primary = pointValue(calculation.primary, bar.time);
      if (primary === undefined) {
        continue;
      }
      if (calculation.kind === "macd") {
        const signal = pointValue(calculation.secondary, bar.time);
        const histogram = pointValue(calculation.histogram, bar.time);
        details.push(
          `${calculation.label}: ${formatPrice(primary)}` +
          `${signal === undefined ? "" : ` / ${formatPrice(signal)}`}` +
          `${histogram === undefined ? "" : ` / H ${formatPrice(histogram)}`}`,
        );
      } else if (calculation.kind === "volume-sma") {
        details.push(`${calculation.label}: ${formatVolume(primary)}`);
      } else {
        details.push(`${calculation.label}: ${formatPrice(primary)}`);
      }
    }
    crosshairDetails.textContent = details.join("  ·  ");
  }

  function setBars(symbol, timeframe, source, values) {
    const nextBars = Array.from(values, normalizeBar);
    const rangesOverlap =
      bars.length > 0 &&
      nextBars.length > 0 &&
      nextBars.at(-1).time >= bars[0].time &&
      nextBars[0].time <= bars.at(-1).time;
    const shouldFit =
      bars.length === 0 ||
      symbolLabel.textContent !== String(symbol) ||
      timeframeLabel.textContent !== String(timeframe) ||
      !rangesOverlap;
    bars = nextBars;
    barIndexByTime = new Map(
      bars.map((bar, index) => [bar.time, index]),
    );
    symbolLabel.textContent = String(symbol);
    timeframeLabel.textContent = String(timeframe);
    sourceLabel.textContent = String(source);
    setSeriesData();
    if (shouldFit) {
      chart.timeScale().fitContent();
    }
    updateCrosshairDetails(bars.at(-1)?.time);
    loading.hidden = true;
  }

  chart.subscribeCrosshairMove((parameter) => {
    updateCrosshairDetails(parameter.time ?? bars.at(-1)?.time);
  });

  function reportError(error) {
    const message = error instanceof Error ? error.message : String(error);
    loading.hidden = false;
    loading.textContent = `Chart error: ${message}`;
    if (bridge !== null && typeof bridge.reportError === "function") {
      bridge.reportError(message);
    }
  }

  window.addEventListener("error", (event) => {
    reportError(event.error ?? event.message);
  });

  try {
    createPriceSeries();
    if (!window.qt?.webChannelTransport) {
      throw new Error("Qt WebChannel transport is unavailable");
    }
    new QWebChannel(window.qt.webChannelTransport, (channel) => {
      bridge = channel.objects.chartBridge;
      bridge.seriesChanged.connect((symbol, timeframe, source, values) => {
        try {
          setBars(symbol, timeframe, source, values);
        } catch (error) {
          reportError(error);
        }
      });
      bridge.themeChanged.connect((value) => {
        try {
          setTheme(value);
        } catch (error) {
          reportError(error);
        }
      });
      bridge.chartStyleChanged.connect((value) => {
        try {
          setStyle(value);
        } catch (error) {
          reportError(error);
        }
      });
      bridge.priceScaleModeChanged.connect((value) => {
        try {
          setPriceScaleMode(value);
        } catch (error) {
          reportError(error);
        }
      });
      bridge.indicatorsChanged.connect((calculations) => {
        try {
          setIndicators(calculations);
        } catch (error) {
          reportError(error);
        }
      });
      bridge.fitRequested.connect(() => chart.timeScale().fitContent());
      bridge.webReady();
    });
  } catch (error) {
    reportError(error);
  }
})();
