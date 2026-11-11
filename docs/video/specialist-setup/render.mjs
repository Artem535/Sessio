// Deterministic frame renderer: node render.mjs stills 5,20,... | node render.mjs video
import { createRequire } from "module";
import { mkdirSync, rmSync, existsSync } from "fs";
import { spawnSync } from "child_process";
import path from "path";
import { fileURLToPath } from "url";
const require = createRequire("/home/a.durynin/.hermes/hermes-agent/node_modules/");
const { chromium } = require("playwright");
const here = path.dirname(fileURLToPath(import.meta.url));
const out = process.env.OUT || path.join(here, "out");
const FPS = 30;
const mode = process.argv[2];

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1280, height: 720 }, deviceScaleFactor: 1.5 });
page.on("pageerror", (e) => console.error("PAGEERROR", e.message));
page.on("console", (m) => { if (m.type() === "error") console.error("CONSOLE", m.text()); });
await page.goto("file://" + path.join(here, "index.html") + "?render=1");
await page.waitForFunction(() => window.READY === true);

if (mode === "stills") {
  mkdirSync(path.join(out, "stills"), { recursive: true });
  for (const t of process.argv[3].split(",")) {
    await page.evaluate((t) => window.setTime(t), parseFloat(t));
    await page.screenshot({ path: path.join(out, "stills", `t${t}.png`) });
  }
} else if (mode === "video") {
  const frames = path.join(out, "frames");
  rmSync(frames, { recursive: true, force: true });
  mkdirSync(frames, { recursive: true });
  const dur = await page.evaluate(() => window.DURATION);
  const n = Math.round(dur * FPS);
  for (let i = 0; i < n; i++) {
    await page.evaluate((t) => window.setTime(t), i / FPS);
    await page.screenshot({ path: path.join(frames, `f${String(i).padStart(5, "0")}.jpg`), type: "jpeg", quality: 95 });
    if (i % 300 === 0) console.log(`frame ${i}/${n}`);
  }
} else if (mode === "cover") {
  await page.evaluate(() => window.setTime(0)); // overwritten below
  await page.goto("file://" + path.join(here, "cover.html"));
  await page.setViewportSize({ width: 1280, height: 720 });
  await page.screenshot({ path: path.join(out, "sessio-specialist-setup.cover.png") });
}
await browser.close();
