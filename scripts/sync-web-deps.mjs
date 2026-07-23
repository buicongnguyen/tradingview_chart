import { copyFile, mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const packageRoot = resolve(root, "node_modules", "lightweight-charts");
const destination = resolve(root, "assets", "web", "vendor");
const metadata = JSON.parse(
  await readFile(resolve(packageRoot, "package.json"), "utf8"),
);

if (metadata.version !== "5.2.0") {
  throw new Error(`Expected lightweight-charts 5.2.0, found ${metadata.version}`);
}

await mkdir(destination, { recursive: true });
await copyFile(
  resolve(packageRoot, "dist", "lightweight-charts.standalone.production.js"),
  resolve(destination, "lightweight-charts.standalone.production.js"),
);
await copyFile(
  resolve(packageRoot, "LICENSE"),
  resolve(destination, "LICENSE.lightweight-charts"),
);
await writeFile(
  resolve(destination, "NOTICE.lightweight-charts"),
  "TradingView Lightweight Charts™ Copyright (с) 2025 TradingView, Inc. https://www.tradingview.com/\n",
  "utf8",
);

console.log(`Synced Lightweight Charts ${metadata.version} into assets/web/vendor.`);
