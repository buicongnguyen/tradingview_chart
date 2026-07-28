import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repositoryRoot = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "..",
  "..",
);
const docsRoot = path.join(repositoryRoot, "docs");

const expectedChapters = [
  "welcome",
  "delivery-plan",
  "architecture",
  "workspace",
  "windows",
  "android",
  "quality",
  "git-workflow",
  "automation",
  "release",
  "pages",
  "operate",
];

async function text(relativePath) {
  return readFile(path.join(repositoryRoot, relativePath), "utf8");
}

function matches(source, expression) {
  return [...source.matchAll(expression)].map((match) => match[1]);
}

test("book contains the expected ordered chapter and navigation structure", async () => {
  const html = await text("docs/index.html");
  const articleIds = matches(
    html,
    /<article\s+class="chapter"\s+id="([^"]+)"\s+data-title="[^"]+">/g,
  );
  const navigationBlock = html.match(
    /<nav id="chapter-nav">([\s\S]*?)<\/nav>/,
  )?.[1];

  assert.ok(navigationBlock, "The chapter navigation must exist.");
  const navigationIds = matches(navigationBlock, /<a href="#([^"]+)"/g);
  assert.deepEqual(articleIds, expectedChapters);
  assert.deepEqual(navigationIds, expectedChapters);
});

test("all IDs are unique and every local fragment resolves", async () => {
  const html = await text("docs/index.html");
  const ids = matches(html, /\sid="([^"]+)"/g);
  const duplicates = ids.filter((id, index) => ids.indexOf(id) !== index);
  assert.deepEqual(duplicates, []);

  const fragments = matches(html, /\shref="#([^"]+)"/g);
  for (const fragment of fragments) {
    assert.ok(ids.includes(fragment), `Missing fragment target #${fragment}`);
  }
});

test("book runtime assets are local, present, and free of CDN imports", async () => {
  const html = await text("docs/index.html");
  const styles = matches(html, /<link[^>]+href="([^"]+)"/g);
  const scripts = matches(html, /<script[^>]+src="([^"]+)"/g);
  assert.deepEqual(styles, ["styles.css"]);
  assert.deepEqual(scripts, ["app.js"]);

  for (const asset of [...styles, ...scripts]) {
    const contents = await readFile(path.join(docsRoot, asset), "utf8");
    assert.ok(contents.length > 500, `${asset} is unexpectedly small.`);
    assert.doesNotMatch(contents, /@import\s+url|https?:\/\/.*\.(?:js|css)/i);
  }
});

test("book has substantial content, accessibility landmarks, and metadata", async () => {
  const html = await text("docs/index.html");
  const styles = await text("docs/styles.css");
  const plainText = html
    .replace(/<script[\s\S]*?<\/script>/g, " ")
    .replace(/<style[\s\S]*?<\/style>/g, " ")
    .replace(/<[^>]+>/g, " ")
    .replace(/\s+/g, " ")
    .trim();
  assert.ok(plainText.split(" ").length > 3000, "Book content is too small.");
  assert.match(html, /<meta name="description"/);
  assert.match(html, /<main id="chapter-content"/);
  assert.match(html, /aria-label="Book navigation"/);
  assert.match(html, /class="skip-link"/);
  assert.match(styles, /@media print/);
  assert.match(styles, /@media \(prefers-reduced-motion: reduce\)/);
});

test("client script avoids dynamic code execution and remote requests", async () => {
  const script = await text("docs/app.js");
  assert.doesNotMatch(script, /\beval\s*\(/);
  assert.doesNotMatch(script, /\bnew\s+Function\b/);
  assert.doesNotMatch(script, /\bfetch\s*\(/);
  assert.doesNotMatch(script, /\bXMLHttpRequest\b/);
  assert.match(script, /localStorage/);
  assert.match(script, /prefers-color-scheme/);
});

test("Pages workflow validates pull requests and deploys docs only from main", async () => {
  const workflow = await text(".github/workflows/pages.yml");
  assert.match(workflow, /pull_request:/);
  assert.match(workflow, /branches:\s*\["main"\]/);
  assert.match(workflow, /node --test tests\/web\/book_site\.test\.mjs/);
  assert.match(workflow, /uses:\s*actions\/checkout@v6/);
  assert.match(workflow, /uses:\s*actions\/setup-node@v6/);
  assert.match(workflow, /uses:\s*actions\/configure-pages@v6/);
  assert.match(workflow, /uses:\s*actions\/upload-pages-artifact@v5/);
  assert.match(workflow, /path:\s*docs/);
  assert.match(workflow, /include-hidden-files:\s*true/);
  assert.match(workflow, /uses:\s*actions\/deploy-pages@v5/);
  assert.match(workflow, /pages:\s*write/);
  assert.match(workflow, /id-token:\s*write/);
  assert.match(workflow, /github\.event_name != 'pull_request'/);
});

test("published files do not contain credential-like assignments", async () => {
  const published = await Promise.all(
    ["docs/index.html", "docs/styles.css", "docs/app.js", "docs/README.md"]
      .map((file) => text(file)),
  );
  const combined = published.join("\n");
  assert.doesNotMatch(combined, /gh[oprsu]_[A-Za-z0-9_]{20,}/);
  assert.doesNotMatch(combined, /-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----/);
  assert.doesNotMatch(
    combined,
    /(?:TWELVE_DATA|ALPHA_VANTAGE|FRED)_API_KEY\s*=\s*['"][A-Za-z0-9_-]{20,}['"]/,
  );
});
