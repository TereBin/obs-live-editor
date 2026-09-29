const CHZZK_API = "https://openapi.chzzk.naver.com";
const CHZZK_AUTHORIZE = "https://chzzk.naver.com/account-interlock";
const DEFAULT_CLIENT_VERSION = "0.1.0";
const DEFAULT_RELEASE_URL = "https://github.com/TereBin/obs-live-editor-releases/releases/latest";
const encoder = new TextEncoder();

function cleanEnvironmentValue(value) {
  return typeof value === "string" ? value.replace(/^\uFEFF/, "") : "";
}

function json(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store",
      "x-content-type-options": "nosniff",
    },
  });
}

function base64UrlEncode(bytes) {
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replaceAll("+", "-").replaceAll("/", "_").replace(/=+$/, "");
}

function base64UrlDecode(value) {
  const padded = value.replaceAll("-", "+").replaceAll("_", "/").padEnd(Math.ceil(value.length / 4) * 4, "=");
  const binary = atob(padded);
  return Uint8Array.from(binary, (character) => character.charCodeAt(0));
}

async function hmac(value, secret) {
  const key = await crypto.subtle.importKey(
    "raw",
    encoder.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign", "verify"],
  );
  return new Uint8Array(await crypto.subtle.sign("HMAC", key, encoder.encode(value)));
}

export async function createSignedValue(payload, secret) {
  const encoded = base64UrlEncode(encoder.encode(JSON.stringify(payload)));
  return `${encoded}.${base64UrlEncode(await hmac(encoded, secret))}`;
}

export async function verifySignedValue(value, secret, purpose, now = Date.now()) {
  try {
    const [encoded, suppliedSignature, extra] = value.split(".");
    if (!encoded || !suppliedSignature || extra) return null;
    const expected = await hmac(encoded, secret);
    const supplied = base64UrlDecode(suppliedSignature);
    if (expected.length !== supplied.length) return null;
    let different = 0;
    for (let index = 0; index < expected.length; index += 1) different |= expected[index] ^ supplied[index];
    if (different !== 0) return null;
    const payload = JSON.parse(new TextDecoder().decode(base64UrlDecode(encoded)));
    if (payload.purpose !== purpose || !Number.isFinite(payload.exp) || payload.exp < now) return null;
    return payload;
  } catch {
    return null;
  }
}

function bearerToken(request) {
  const authorization = request.headers.get("authorization") ?? "";
  return authorization.startsWith("Bearer ") ? authorization.slice(7) : "";
}

function versionParts(version) {
  if (typeof version !== "string" || !/^\d+\.\d+\.\d+$/.test(version)) return null;
  return version.split(".").map(Number);
}

export function compareVersions(left, right) {
  const leftParts = versionParts(left);
  const rightParts = versionParts(right);
  if (!leftParts || !rightParts) return null;
  for (let index = 0; index < 3; index += 1) {
    if (leftParts[index] !== rightParts[index]) return leftParts[index] < rightParts[index] ? -1 : 1;
  }
  return 0;
}

function clientUpdateResponse(request, config) {
  const clientVersion = cleanEnvironmentValue(request.headers.get("x-obs-live-editor-version")) || DEFAULT_CLIENT_VERSION;
  const comparison = compareVersions(clientVersion, config.MINIMUM_CLIENT_VERSION);
  if (comparison !== null && comparison >= 0) return null;
  return json({
    code: "CLIENT_UPDATE_REQUIRED",
    latestVersion: config.LATEST_CLIENT_VERSION,
    minimumVersion: config.MINIMUM_CLIENT_VERSION,
    level: config.CLIENT_UPDATE_LEVEL === "security" ? "security" : "required",
    message: config.CLIENT_UPDATE_MESSAGE || "This version is no longer supported. Please update the plugin.",
    releaseUrl: config.CLIENT_RELEASE_URL,
  }, 426);
}

async function parseJson(request) {
  try {
    return await request.json();
  } catch {
    return null;
  }
}

async function chzzkRequest(path, init) {
  const response = await fetch(`${CHZZK_API}${path}`, init);
  const text = await response.text();
  let body;
  try {
    body = text ? JSON.parse(text) : {};
  } catch {
    body = { message: "CHZZK returned an invalid response" };
  }
  return { response, body };
}

async function startAuthorization(env) {
  const now = Date.now();
  const state = await createSignedValue(
    { purpose: "oauth-state", exp: now + 5 * 60 * 1000, nonce: crypto.randomUUID() },
    env.STATE_SECRET,
  );
  const url = new URL(CHZZK_AUTHORIZE);
  url.searchParams.set("clientId", env.CHZZK_CLIENT_ID);
  url.searchParams.set("redirectUri", env.CHZZK_REDIRECT_URI);
  url.searchParams.set("state", state);
  return json({ state, authorizationUrl: url.toString() });
}

async function exchangeToken(request, env) {
  const input = await parseJson(request);
  if (!input || !["authorization_code", "refresh_token"].includes(input.grantType)) {
    return json({ message: "Invalid token request" }, 400);
  }

  const body = {
    grantType: input.grantType,
    clientId: env.CHZZK_CLIENT_ID,
    clientSecret: env.CHZZK_CLIENT_SECRET,
  };
  if (input.grantType === "authorization_code") {
    const state = await verifySignedValue(input.state ?? "", env.STATE_SECRET, "oauth-state");
    if (!state || typeof input.code !== "string" || !input.code) {
      return json({ message: "Invalid or expired OAuth state" }, 400);
    }
    body.code = input.code;
    body.state = input.state;
  } else {
    if (typeof input.refreshToken !== "string" || !input.refreshToken) {
      return json({ message: "Refresh token is required" }, 400);
    }
    body.refreshToken = input.refreshToken;
  }

  const upstream = await chzzkRequest("/auth/v1/token", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  if (!upstream.response.ok) return json(upstream.body, upstream.response.status);

  const tokenBody = upstream.body?.content && typeof upstream.body.content === "object"
    ? upstream.body.content
    : upstream.body;
  const brokerToken = await createSignedValue(
    { purpose: "category-search", exp: Date.now() + 2 * 24 * 60 * 60 * 1000, nonce: crypto.randomUUID() },
    env.STATE_SECRET,
  );
  return json({ ...tokenBody, brokerToken });
}

async function revokeToken(request, env) {
  const input = await parseJson(request);
  if (!input || typeof input.token !== "string" || !input.token) {
    return json({ message: "Token is required" }, 400);
  }
  const upstream = await chzzkRequest("/auth/v1/token/revoke", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      clientId: env.CHZZK_CLIENT_ID,
      clientSecret: env.CHZZK_CLIENT_SECRET,
      token: input.token,
      tokenTypeHint: input.tokenTypeHint === "refresh_token" ? "refresh_token" : "access_token",
    }),
  });
  return json(upstream.body, upstream.response.status);
}

async function searchCategories(request, env) {
  if (!(await verifySignedValue(bearerToken(request), env.STATE_SECRET, "category-search"))) {
    return json({ message: "Unauthorized" }, 401);
  }
  const query = new URL(request.url).searchParams.get("query")?.trim() ?? "";
  if (query.length < 2 || query.length > 100) return json({ message: "Invalid query" }, 400);

  const url = new URL(`${CHZZK_API}/open/v1/categories/search`);
  url.searchParams.set("query", query);
  url.searchParams.set("size", "20");
  const upstream = await fetch(url, {
    headers: {
      "Client-Id": env.CHZZK_CLIENT_ID,
      "Client-Secret": env.CHZZK_CLIENT_SECRET,
      Accept: "application/json",
    },
  });
  return new Response(await upstream.text(), {
    status: upstream.status,
    headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" },
  });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const config = {
      CHZZK_CLIENT_ID: cleanEnvironmentValue(env.CHZZK_CLIENT_ID),
      CHZZK_CLIENT_SECRET: cleanEnvironmentValue(env.CHZZK_CLIENT_SECRET),
      CHZZK_REDIRECT_URI: cleanEnvironmentValue(env.CHZZK_REDIRECT_URI),
      STATE_SECRET: cleanEnvironmentValue(env.STATE_SECRET),
      MINIMUM_CLIENT_VERSION: cleanEnvironmentValue(env.MINIMUM_CLIENT_VERSION) || DEFAULT_CLIENT_VERSION,
      LATEST_CLIENT_VERSION: cleanEnvironmentValue(env.LATEST_CLIENT_VERSION) || DEFAULT_CLIENT_VERSION,
      CLIENT_UPDATE_LEVEL: cleanEnvironmentValue(env.CLIENT_UPDATE_LEVEL) || "optional",
      CLIENT_UPDATE_MESSAGE: cleanEnvironmentValue(env.CLIENT_UPDATE_MESSAGE),
      CLIENT_RELEASE_URL: cleanEnvironmentValue(env.CLIENT_RELEASE_URL) || DEFAULT_RELEASE_URL,
    };
    if (!config.CHZZK_CLIENT_ID || !config.CHZZK_CLIENT_SECRET || !config.CHZZK_REDIRECT_URI || !config.STATE_SECRET) {
      return json({ message: "Worker is not configured" }, 503);
    }
    const supportedRoute = ["/oauth/start", "/oauth/token", "/oauth/revoke", "/categories"].includes(url.pathname);
    if (supportedRoute) {
      const updateResponse = clientUpdateResponse(request, config);
      if (updateResponse) return updateResponse;
    }
    if (request.method === "GET" && url.pathname === "/oauth/start") return startAuthorization(config);
    if (request.method === "POST" && url.pathname === "/oauth/token") return exchangeToken(request, config);
    if (request.method === "POST" && url.pathname === "/oauth/revoke") return revokeToken(request, config);
    if (request.method === "GET" && url.pathname === "/categories") return searchCategories(request, config);
    return json({ message: "Not found" }, 404);
  },
};
