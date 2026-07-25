import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const indexUrl = new URL("../../assets/web/index.html", import.meta.url);
const chartUrl = new URL("../../assets/web/chart.js", import.meta.url);

test("embedded page has no runtime CDN dependency", async () => {
  const html = await readFile(indexUrl, "utf8");
  assert.doesNotMatch(html, /https?:\/\//i);
  assert.match(html, /connect-src 'none'/);
  assert.match(html, /vendor\/lightweight-charts\.standalone\.production\.js/);
  assert.match(html, /id="indicator"/);
});

test("renderer keeps attribution and receives application data", async () => {
  const source = await readFile(chartUrl, "utf8");
  assert.doesNotThrow(() => new Function(source));
  assert.match(source, /attributionLogo:\s*true/);
  assert.match(source, /priceSeries\.setData/);
  assert.match(source, /bridge\.seriesChanged\.connect/);
  assert.match(source, /bridge\.indicatorChanged\.connect/);
  assert.match(source, /LightweightCharts\.LineSeries/);
  assert.match(source, /LightweightCharts\.HistogramSeries/);
  assert.match(source, /chart\.removePane/);
  assert.match(source, /sourceLabel\.textContent\s*=\s*String\(source\)/);
  assert.match(source, /const rangesOverlap\s*=/);
  assert.match(source, /if \(shouldFit\)/);
  assert.doesNotMatch(source, /\bfetch\s*\(/);
  assert.doesNotMatch(source, /XMLHttpRequest/);
});
