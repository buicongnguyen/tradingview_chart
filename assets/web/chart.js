(() => {
  "use strict";

  const container = document.getElementById("chart");
  const loading = document.getElementById("loading");
  const symbolLabel = document.getElementById("symbol");
  const timeframeLabel = document.getElementById("timeframe");
  const indicatorLabel = document.getElementById("indicator");
  const sourceLabel = document.getElementById("source");

  let bridge = null;
  let dark = true;
  let style = "candlestick";
  let bars = [];
  let priceSeries = null;
  let indicatorSeries = [];

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
    setSeriesData();
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

  function setIndicator(calculation) {
    clearIndicatorSeries();
    const kind = String(calculation?.kind ?? "none");
    const label = String(calculation?.label ?? "None");
    const primary = indicatorPoints(calculation?.primary);
    const secondary = indicatorPoints(calculation?.secondary);
    const histogram = indicatorPoints(calculation?.histogram);
    indicatorLabel.textContent =
      kind !== "none" && primary.length === 0
        ? `${label} · warming up`
        : label;

    if (kind === "none" || primary.length === 0) {
      return;
    }

    if (["sma", "ema", "vwap"].includes(kind)) {
      const color = kind === "sma"
        ? "#ff9800"
        : kind === "ema"
          ? "#ab47bc"
          : "#00acc1";
      const overlay = chart.addSeries(LightweightCharts.LineSeries, {
        color,
        lineWidth: 2,
        lastValueVisible: true,
        priceLineVisible: false,
        crosshairMarkerVisible: true,
      });
      overlay.setData(primary);
      indicatorSeries.push(overlay);
      return;
    }

    if (kind === "rsi") {
      const rsi = chart.addSeries(LightweightCharts.LineSeries, {
        color: "#7e57c2",
        lineWidth: 2,
        lastValueVisible: true,
        priceLineVisible: false,
      }, 1);
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
        1,
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
      }, 1);
      macd.setData(primary);
      const signal = chart.addSeries(LightweightCharts.LineSeries, {
        color: "#42a5f5",
        lineWidth: 2,
        lastValueVisible: true,
        priceLineVisible: false,
      }, 1);
      signal.setData(secondary);
      indicatorSeries.push(histogramSeries, macd, signal);
    }

    const panes = chart.panes();
    if (panes.length > 1) {
      panes[0].setStretchFactor(3);
      panes[1].setStretchFactor(1);
    }
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
    symbolLabel.textContent = String(symbol);
    timeframeLabel.textContent = String(timeframe);
    sourceLabel.textContent = String(source);
    setSeriesData();
    if (shouldFit) {
      chart.timeScale().fitContent();
    }
    loading.hidden = true;
  }

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
      bridge.indicatorChanged.connect((calculation) => {
        try {
          setIndicator(calculation);
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
