// Live cluster view. From the repo: node deploy/dashboard/server.js
import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { execFile } from "node:child_process";
import { fileURLToPath } from "node:url";

const PORT = Number(process.env.DASHBOARD_PORT || 3000);
const BIND = process.env.DASHBOARD_BIND || "127.0.0.1";
const GATEWAY = process.env.GATEWAY_ADMIN || "http://127.0.0.1:8090";
const HERE = path.dirname(fileURLToPath(import.meta.url));
const LOGS_ROOT = process.env.LOGS_ROOT || path.resolve(HERE, "../../env/dist/logs");
const GATEWAY_CONTAINER = process.env.GATEWAY_CONTAINER || "sf-clustergateway";

const MAP_NAMES = {
  0: "Eastern Kingdoms",
  1: "Kalimdor",
  30: "Alterac Valley",
  489: "Warsong Gulch",
  529: "Arathi Basin",
  530: "Outland",
  566: "Eye of the Storm",
  571: "Northrend",
  607: "Strand of the Ancients",
  628: "Isle of Conquest",
  726: "Twin Peaks",
  727: "Silvershard Mines",
  761: "Temple of Kotmogu",
  860: "Wandering Isle",
  870: "Pandaria",
  998: "Temple of Kotmogu (scenario)",
  1105: "Deepwind Gorge",
};

const events = [];
let previous = null;

function note(text) {
  events.unshift({ t: Date.now(), text });
  if (events.length > 100)
    events.pop();
}

function exec(cmd, args) {
  return new Promise((resolve, reject) => {
    execFile(cmd, args, { timeout: 5000, maxBuffer: 2 * 1024 * 1024 }, (err, stdout, stderr) => {
      if (err)
        reject(Object.assign(err, { stdout, stderr }));
      else
        resolve(stdout);
    });
  });
}

function stripAnsi(text) {
  return text.replace(/\u001b\[[0-9;]*m/g, "");
}

function mapName(id) {
  return MAP_NAMES[id] || `map ${id}`;
}

function shortNode(name) {
  return String(name || "").replace(/^sf-world-/, "");
}

async function fetchJson(url) {
  const res = await fetch(url, { signal: AbortSignal.timeout(2000) });
  if (!res.ok)
    throw new Error(`${res.status}`);
  return res.json();
}

function readTail(file, maxLines) {
  let stat;
  try {
    stat = fs.statSync(file);
  } catch {
    return [];
  }
  if (!stat.size)
    return [];

  const len = Math.min(stat.size, 48 * 1024);
  const fd = fs.openSync(file, "r");
  try {
    const buf = Buffer.alloc(len);
    fs.readSync(fd, buf, 0, len, stat.size - len);
    let text = buf.toString("utf8");
    if (stat.size > len) {
      const nl = text.indexOf("\n");
      if (nl !== -1)
        text = text.slice(nl + 1);
    }
    return text.split("\n").map((line) => line.trimEnd()).filter((line) => line.length).slice(-maxLines);
  } finally {
    fs.closeSync(fd);
  }
}

function collectLogFile(dir, name) {
  const candidates = ["Server.log", "Gateway.log", "Auth.log", "server.log", "gateway.log"];
  for (const file of candidates) {
    const lines = readTail(path.join(dir, file), 80);
    if (lines.length)
      return { name, lines };
  }
  return null;
}

async function loadLogs() {
  const sources = [];
  let root = LOGS_ROOT;
  try {
    if (fs.existsSync(root) && fs.statSync(root).isDirectory()) {
      const auth = collectLogFile(root, "auth") || collectLogFile(path.join(root, "auth"), "auth");
      if (auth)
        sources.push(auth);
      for (const dir of fs.readdirSync(root, { withFileTypes: true })) {
        if (!dir.isDirectory())
          continue;
        const packed = collectLogFile(path.join(root, dir.name), dir.name);
        if (packed)
          sources.push(packed);
      }
    }
  } catch {
    // Log directory is optional until the stack has written files.
  }
  try {
    const out = stripAnsi(await exec("docker", ["logs", "--tail", "80", GATEWAY_CONTAINER]));
    const lines = out.split("\n").map((line) => line.trimEnd()).filter((line) => line.length);
    if (lines.length)
      sources.push({ name: "gateway", lines });
  } catch {
    // docker CLI is optional inside the dashboard container.
  }
  return sources;
}

async function loadCluster() {
  const [nodes, maps, routes, players] = await Promise.all([
    fetchJson(`${GATEWAY}/v1/nodes`),
    fetchJson(`${GATEWAY}/v1/maps`),
    fetchJson(`${GATEWAY}/v1/routes`).catch(() => []),
    fetchJson(`${GATEWAY}/v1/players`).catch(() => null),
  ]);

  const mapsByNode = new Map();
  for (const row of maps) {
    const key = row.nodeName;
    const list = mapsByNode.get(key) || [];
    list.push({ mapId: row.mapId, mapName: mapName(row.mapId) });
    mapsByNode.set(key, list);
  }

  const countByMap = new Map();
  if (players)
    for (const row of players)
      countByMap.set(row.mapId, Number(row.count) || 0);

  const detailed = nodes.map((node) => {
    const owned = (mapsByNode.get(node.name) || []).sort((a, b) => a.mapId - b.mapId);
    const playerCount = owned.reduce((sum, map) => sum + (countByMap.get(map.mapId) || 0), 0);
    return {
      ...node,
      name: shortNode(node.name) || node.name,
      maps: owned,
      players: players ? playerCount : null,
      health: node.online ? "ok" : "down",
    };
  });

  const nodeByMap = new Map();
  for (const node of detailed)
    for (const map of node.maps)
      nodeByMap.set(map.mapId, node.name);

  const namedRoutes = routes.map((route) => ({
    ...route,
    mapName: mapName(route.mapId),
    node: nodeByMap.get(route.mapId) || shortNode(route.nodeName) || null,
  }));

  return { nodes: detailed, routes: namedRoutes };
}

function diffSnapshot(snapshot) {
  if (!previous) {
    previous = snapshot;
    return;
  }
  for (const node of snapshot.nodes) {
    const before = previous.nodes.find((row) => row.name === node.name);
    if (before && before.players !== node.players && node.players !== null)
      note(`${node.name} players ${before.players ?? "?"} → ${node.players}`);
    if (before && before.health !== node.health)
      note(`${node.name} ${before.health} → ${node.health}`);
  }
  const prevRoutes = new Map(previous.routes.map((row) => [row.accountId, row]));
  for (const route of snapshot.routes) {
    const before = prevRoutes.get(route.accountId);
    if (!before)
      note(`${route.username || "account " + route.accountId} route set to ${route.mapName} (${route.mapId})`);
    else if (before.mapId !== route.mapId)
      note(`${route.username || "account " + route.accountId} route ${before.mapName} (${before.mapId}) → ${route.mapName} (${route.mapId})`);
  }
  previous = snapshot;
}

async function snapshot() {
  const errors = [];
  let nodes = [];
  let routes = [];
  let logs = [];
  try {
    const cluster = await loadCluster();
    nodes = cluster.nodes;
    routes = cluster.routes;
  } catch (err) {
    errors.push(`gateway: ${err.message || err}`);
  }
  try {
    logs = await loadLogs();
  } catch (err) {
    errors.push(`logs: ${err.message || err}`);
  }

  const body = {
    now: Date.now(),
    nodes,
    routes,
    logs,
    events,
    errors,
  };
  diffSnapshot(body);
  body.events = events;
  return body;
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on("data", (chunk) => chunks.push(chunk));
    req.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")));
    req.on("error", reject);
  });
}

async function proxyPost(req, res, gatewayPath) {
  try {
    const raw = await readBody(req);
    const upstream = await fetch(`${GATEWAY}${gatewayPath}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: raw,
      signal: AbortSignal.timeout(5000),
    });
    const body = await upstream.text();
    res.writeHead(upstream.status, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
    res.end(body);
  } catch (err) {
    res.writeHead(502, { "Content-Type": "application/json; charset=utf-8" });
    res.end(JSON.stringify({ error: err.message || String(err) }));
  }
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://127.0.0.1:${PORT}`);
  if (req.method === "POST" && url.pathname === "/api/maps/split") {
    await proxyPost(req, res, "/v1/maps/split");
    return;
  }
  if (req.method === "POST" && url.pathname === "/api/maps/merge") {
    await proxyPost(req, res, "/v1/maps/merge");
    return;
  }
  if (url.pathname === "/api/snapshot") {
    try {
      const body = JSON.stringify(await snapshot());
      res.writeHead(200, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
      res.end(body);
    } catch (err) {
      res.writeHead(500, { "Content-Type": "application/json; charset=utf-8" });
      res.end(JSON.stringify({ error: err.message || String(err) }));
    }
    return;
  }
  if (url.pathname === "/" || url.pathname === "/index.html") {
    const html = fs.readFileSync(path.join(HERE, "index.html"));
    res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
    res.end(html);
    return;
  }
  res.writeHead(404);
  res.end("not found");
});

server.listen(PORT, BIND, () => {
  console.log(`Cluster dashboard http://${BIND}:${PORT}`);
});
