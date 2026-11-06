#!/usr/bin/env node
// Record the web viewer replaying a trace as a GIF (used for docs/images/viewer.gif).
//
//   node tools/record_viewer.mjs run.jsonl viewer.gif [--frames 60] [--fps 10] [--width 1280] [--hold 2.5] [--light]
//
// Starts a local static server and headless Firefox (driven over WebDriver BiDi),
// screenshots the viewer at 2x pixel density while it steps through the run, and
// encodes the frames with ffmpeg using a single optimised palette.
// Requires Node >= 22 (built-in WebSocket), Firefox >= 119 and ffmpeg.
// Set FIREFOX=/path/to/firefox to pick the browser binary.
import { spawn, execFileSync } from "node:child_process";
import { createServer } from "node:http";
import { copyFileSync, mkdtempSync, readFileSync, rmSync, writeFileSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, extname, join, normalize, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const usage = "usage: record_viewer.mjs <trace.jsonl> <out.gif> [--frames N] [--fps N] [--width PX] [--hold SEC] [--light]";
const argv = process.argv.slice(2);
const opt = { frames: 60, fps: 10, width: 1280, hold: 2.5 };
const pos = [];
let light = false;
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === "-h" || a === "--help") { console.log(usage); process.exit(0); }
  if (a === "--light") { light = true; continue; }
  if (a.startsWith("--") && a.slice(2) in opt) opt[a.slice(2)] = Number(argv[++i]);
  else pos.push(a);
}
if (pos.length !== 2 || Object.values(opt).some((v) => !(v > 0))) { console.error(usage); process.exit(2); }
const [tracePath, outPath] = pos.map((p) => resolve(p));
const VIEW_W = 1280, VIEW_H = 800; // CSS pixels; screenshots are 2x this

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const work = mkdtempSync(join(tmpdir(), "axiom-rec-"));
let firefox = null, server = null;
const cleanup = () => {
  if (firefox) firefox.kill("SIGKILL");
  if (server) server.close();
  rmSync(work, { recursive: true, force: true });
};
process.on("SIGINT", () => { cleanup(); process.exit(130); });

try {
  // --------------------------------------------------------------- static server
  const types = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".svg": "image/svg+xml" };
  server = createServer((req, res) => {
    const path = decodeURIComponent(new URL(req.url, "http://x").pathname);
    const file = path === "/trace.jsonl" ? tracePath : join(root, normalize(path).replace(/^(\.\.[/\\])+/, ""));
    const target = existsSync(file) && !file.endsWith("/") ? file : join(file, "index.html");
    try {
      const body = readFileSync(target);
      res.writeHead(200, { "content-type": types[extname(target)] || "application/octet-stream" });
      res.end(body);
    } catch {
      res.writeHead(404); res.end();
    }
  });
  await new Promise((r) => server.listen(0, "127.0.0.1", r));
  const url = `http://127.0.0.1:${server.address().port}/viewer/?trace=/trace.jsonl`;

  // --------------------------------------------------------------- browser
  const profile = join(work, "profile");
  execFileSync("mkdir", ["-p", profile]);
  writeFileSync(join(profile, "user.js"), [
    'user_pref("layout.css.devPixelsPerPx", "2");',
    // 0 = dark, 1 = light; otherwise a fresh profile follows the desktop theme
    `user_pref("layout.css.prefers-color-scheme.content-override", ${light ? 1 : 0});`,
    'user_pref("browser.shell.checkDefaultBrowser", false);',
    'user_pref("datareporting.policy.dataSubmissionEnabled", false);',
  ].join("\n"));
  firefox = spawn(process.env.FIREFOX || "firefox",
    ["--headless", "--no-remote", "--profile", profile, "--remote-debugging-port=0"],
    { stdio: ["ignore", "pipe", "pipe"] });
  const wsUrl = await new Promise((res, rej) => {
    let log = "";
    const timer = setTimeout(() => rej(new Error(`firefox did not start:\n${log}`)), 30000);
    const onData = (d) => {
      log += d;
      const m = log.match(/WebDriver BiDi listening on (ws:\/\/\S+)/);
      if (m) { clearTimeout(timer); res(m[1].replace(/\/$/, "") + "/session"); }
    };
    firefox.stdout.on("data", onData);
    firefox.stderr.on("data", onData);
    firefox.on("error", (e) => { clearTimeout(timer); rej(e); });
  });

  const ws = new WebSocket(wsUrl);
  let nextId = 0;
  const pending = new Map();
  ws.onmessage = (m) => {
    const msg = JSON.parse(m.data);
    const p = pending.get(msg.id);
    if (!p) return;
    pending.delete(msg.id);
    msg.type === "error" ? p.rej(new Error(`${msg.error}: ${msg.message}`)) : p.res(msg.result);
  };
  const send = (method, params = {}) => new Promise((res, rej) => {
    const id = ++nextId;
    pending.set(id, { res, rej });
    ws.send(JSON.stringify({ id, method, params }));
  });
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error(`cannot connect to ${wsUrl}`)); });

  await send("session.new", { capabilities: {} });
  const context = (await send("browsingContext.getTree", {})).contexts[0].context;
  await send("browsingContext.setViewport", { context, viewport: { width: VIEW_W, height: VIEW_H } });
  await send("browsingContext.navigate", { context, url, wait: "complete" });
  const evaluate = async (expression) => {
    const r = await send("script.evaluate", { expression, target: { context }, awaitPromise: true });
    if (r.type === "exception") throw new Error(r.exceptionDetails.text);
    return r.result.value;
  };

  let total = 0;
  for (let i = 0; i < 100 && !(total = await evaluate("S.frames.length")); i++) await new Promise((r) => setTimeout(r, 100));
  if (!total) throw new Error(`the viewer could not load ${tracePath}`);
  // Hide scrollbars and show the pause icon so the recording reads as playback.
  await evaluate(`document.head.insertAdjacentHTML("beforeend", "<style>*{scrollbar-width:none!important}</style>");
                  setPlaying(true); S.playing = false; 0`);

  // --------------------------------------------------------------- capture
  // Ease through the run: dense at the start, where the layout changes most.
  const n = Math.min(opt.frames, total);
  const idx = [];
  for (let k = 0; k < n; k++) idx.push(Math.round(Math.pow(k / Math.max(n - 1, 1), 1.7) * (total - 1)));
  const holdFrames = Math.round(opt.hold * opt.fps);
  let k = 0;
  const shot = async () => {
    const { data } = await send("browsingContext.captureScreenshot", { context });
    const file = join(work, `f${String(k++).padStart(5, "0")}.png`);
    writeFileSync(file, Buffer.from(data, "base64"));
    return file;
  };
  for (let j = 0; j < idx.length; j++) {
    await evaluate(`seek(${idx[j]}); new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)))`);
    const file = await shot();
    process.stdout.write(`\rcaptured ${j + 1}/${idx.length}`);
    if (j === idx.length - 1) for (let h = 0; h < holdFrames; h++) copyFileSync(file, join(work, `f${String(k++).padStart(5, "0")}.png`));
  }
  process.stdout.write("\n");
  await send("session.end", {}).catch(() => {});
  ws.close();

  // --------------------------------------------------------------- encode
  const input = ["-loglevel", "error", "-y", "-framerate", String(opt.fps), "-i", join(work, "f%05d.png")];
  const scale = `scale=${opt.width}:-1:flags=lanczos`;
  const palette = join(work, "palette.png");
  execFileSync("ffmpeg", [...input, "-vf", `${scale},palettegen=max_colors=256:stats_mode=full`, palette], { stdio: "inherit" });
  execFileSync("ffmpeg", [...input, "-i", palette, "-lavfi", `${scale}[x];[x][1:v]paletteuse=dither=none:diff_mode=rectangle`, outPath], { stdio: "inherit" });
  console.log(`${outPath}: ${k} frames, ${(readFileSync(outPath).length / 1e6).toFixed(1)} MB`);
} catch (err) {
  console.error(`record_viewer: ${err.message}`);
  process.exitCode = 1;
} finally {
  cleanup();
}
