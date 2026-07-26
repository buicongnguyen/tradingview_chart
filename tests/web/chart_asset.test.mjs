import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const indexUrl = new URL("../../assets/web/index.html", import.meta.url);
const mobileUrl = new URL("../../android/assets/web/mobile.html", import.meta.url);
const chartUrl = new URL("../../assets/web/chart.js", import.meta.url);

test("embedded page has no runtime CDN dependency", async () => {
  const html = await readFile(indexUrl, "utf8");
  assert.doesNotMatch(html, /https?:\/\//i);
  assert.match(html, /connect-src 'none'/);
  assert.match(html, /vendor\/lightweight-charts\.standalone\.production\.js/);
  assert.match(html, /id="indicator"/);
  assert.match(html, /id="crosshair-details"/);
});

test("Android page is local-only and selects the native mobile host", async () => {
  const html = await readFile(mobileUrl, "utf8");
  assert.doesNotMatch(html, /https?:\/\//i);
  assert.match(html, /connect-src 'none'/);
  assert.match(html, /data-host="mobile"/);
  assert.doesNotMatch(html, /qwebchannel/i);
  assert.match(html, /vendor\/lightweight-charts\.standalone\.production\.js/);
});

test("renderer keeps attribution and receives application data", async () => {
  const source = await readFile(chartUrl, "utf8");
  assert.doesNotThrow(() => new Function(source));
  assert.match(source, /attributionLogo:\s*true/);
  assert.match(source, /priceSeries\.setData/);
  assert.match(source, /bridge\.seriesChanged\.connect/);
  assert.match(source, /bridge\.indicatorsChanged\.connect/);
  assert.match(source, /bridge\.researchEventsChanged\.connect/);
  assert.match(source, /bridge\.marketStructureChanged\.connect/);
  assert.match(source, /bridge\.priceLevelsChanged\.connect/);
  assert.match(source, /bridge\.visibleRangeChanged\.connect/);
  assert.match(source, /bridge\.crosshairTimeChanged\.connect/);
  assert.match(source, /bridge\.priceScaleModeChanged\.connect/);
  assert.match(source, /window\.mobileChart = Object\.freeze/);
  assert.match(source, /receiveMobileCommand/);
  assert.match(source, /chart\.subscribeCrosshairMove/);
  assert.match(source, /volumeRatio/);
  assert.match(source, /PriceScaleMode\.Logarithmic/);
  assert.match(source, /rolling-high/);
  assert.match(source, /volume-sma/);
  assert.match(source, /LightweightCharts\.LineSeries/);
  assert.match(source, /LightweightCharts\.HistogramSeries/);
  assert.match(source, /LightweightCharts\.createSeriesMarkers/);
  assert.match(source, /priceSeries\.createPriceLine/);
  assert.match(source, /priceSeries\.attachPrimitive/);
  assert.match(source, /class MarketStructurePrimitive/);
  assert.match(source, /useMediaCoordinateSpace/);
  assert.match(source, /const maximumOverlayLabels\s*=\s*6/);
  assert.match(source, /function drawOverlayLabels/);
  assert.match(source, /identities\.has\(candidate\.title\)/);
  assert.match(source, /const overlaps\s*=\s*placed\.some/);
  assert.match(source, /subscribeVisibleTimeRangeChange/);
  assert.match(source, /chart\.setCrosshairPosition/);
  assert.match(source, /chart\.removePane/);
  assert.match(source, /sourceLabel\.textContent\s*=\s*String\(source\)/);
  assert.match(source, /const rangesOverlap\s*=/);
  assert.match(source, /if \(shouldFit\)/);
  assert.doesNotMatch(source, /\bfetch\s*\(/);
  assert.doesNotMatch(source, /XMLHttpRequest/);
});
