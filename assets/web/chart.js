(() => {
  "use strict";

  const container = document.getElementById("chart");
  const loading = document.getElementById("loading");
  const symbolLabel = document.getElementById("symbol");
  const timeframeLabel = document.getElementById("timeframe");
  const sourceLabel = document.getElementById("source");

  let bridge = null;
  let dark = true;
  let style = "candlestick";
  let bars = [];
  let priceSeries = null;

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
      bridge.fitRequested.connect(() => chart.timeScale().fitContent());
      bridge.webReady();
    });
  } catch (error) {
    reportError(error);
  }
})();
