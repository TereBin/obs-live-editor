import test from "node:test";
import assert from "node:assert/strict";

import worker, { compareVersions, createSignedValue, verifySignedValue } from "../src/index.mjs";

globalThis.btoa ??= (value) => Buffer.from(value, "binary").toString("base64");
globalThis.atob ??= (value) => Buffer.from(value, "base64").toString("binary");

const secret = "test-secret-with-enough-entropy";
const env = {
  CHZZK_CLIENT_ID: "test-client",
  CHZZK_CLIENT_SECRET: "test-client-secret",
  CHZZK_REDIRECT_URI: "http://127.0.0.1:20132/callback",
  STATE_SECRET: secret,
  MINIMUM_CLIENT_VERSION: "0.1.0",
  LATEST_CLIENT_VERSION: "0.2.0",
  CLIENT_UPDATE_LEVEL: "optional",
  CLIENT_UPDATE_MESSAGE: "A new version is available.",
  CLIENT_RELEASE_URL: "https://github.com/TereBin/obs-live-editor-releases/releases/latest",
};

test("semantic versions compare by numeric components", () => {
  assert.equal(compareVersions("0.2.0", "0.1.9"), 1);
  assert.equal(compareVersions("0.2.0", "0.2.0"), 0);
  assert.equal(compareVersions("0.2.0", "1.0.0"), -1);
  assert.equal(compareVersions("invalid", "1.0.0"), null);
});

test("unsupported clients receive an upgrade response", async () => {
  const policy = { ...env, MINIMUM_CLIENT_VERSION: "0.2.0" };
  const request = new Request("https://broker.example/oauth/start", {
    headers: { "X-OBS-Live-Editor-Version": "0.1.0" },
  });
  const response = await worker.fetch(request, policy);
  assert.equal(response.status, 426);
  const body = await response.json();
  assert.equal(body.code, "CLIENT_UPDATE_REQUIRED");
  assert.equal(body.minimumVersion, "0.2.0");
  assert.equal(body.level, "required");
});

test("missing and malformed client versions are treated as unsupported when the minimum increases", async () => {
  const policy = { ...env, MINIMUM_CLIENT_VERSION: "0.2.0" };
  const missing = await worker.fetch(new Request("https://broker.example/oauth/start"), policy);
  assert.equal(missing.status, 426);
  const malformed = await worker.fetch(new Request("https://broker.example/oauth/start", {
    headers: { "X-OBS-Live-Editor-Version": "development" },
  }), policy);
  assert.equal(malformed.status, 426);
});

test("supported clients continue to OAuth", async () => {
  const request = new Request("https://broker.example/oauth/start", {
    headers: { "X-OBS-Live-Editor-Version": "0.2.0" },
  });
  const response = await worker.fetch(request, env);
  assert.equal(response.status, 200);
});

test("signed values round trip", async () => {
  const value = await createSignedValue({ purpose: "oauth-state", exp: Date.now() + 10_000 }, secret);
  const payload = await verifySignedValue(value, secret, "oauth-state");
  assert.equal(payload.purpose, "oauth-state");
});

test("tampered values are rejected", async () => {
  const value = await createSignedValue({ purpose: "oauth-state", exp: Date.now() + 10_000 }, secret);
  const [payload, signature] = value.split(".");
  const tampered = `${payload}.${signature.startsWith("a") ? "b" : "a"}${signature.slice(1)}`;
  assert.equal(await verifySignedValue(tampered, secret, "oauth-state"), null);
});

test("expired and wrong-purpose values are rejected", async () => {
  const expired = await createSignedValue({ purpose: "oauth-state", exp: Date.now() - 1 }, secret);
  assert.equal(await verifySignedValue(expired, secret, "oauth-state"), null);

  const valid = await createSignedValue({ purpose: "category-search", exp: Date.now() + 10_000 }, secret);
  assert.equal(await verifySignedValue(valid, secret, "oauth-state"), null);
});

test("OAuth start returns a verifiable state and exact redirect URI", async () => {
  const response = await worker.fetch(new Request("https://broker.example/oauth/start"), env);
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.ok(await verifySignedValue(body.state, secret, "oauth-state"));
  const authorizationUrl = new URL(body.authorizationUrl);
  assert.equal(authorizationUrl.origin, "https://chzzk.naver.com");
  assert.equal(authorizationUrl.searchParams.get("clientId"), env.CHZZK_CLIENT_ID);
  assert.equal(authorizationUrl.searchParams.get("redirectUri"), env.CHZZK_REDIRECT_URI);
});

test("environment values tolerate a PowerShell UTF-8 BOM", async () => {
  const bomEnv = Object.fromEntries(Object.entries(env).map(([key, value]) => [key, `\uFEFF${value}`]));
  const response = await worker.fetch(new Request("https://broker.example/oauth/start"), bomEnv);
  assert.equal(response.status, 200);
  const body = await response.json();
  const authorizationUrl = new URL(body.authorizationUrl);
  assert.equal(authorizationUrl.searchParams.get("clientId"), env.CHZZK_CLIENT_ID);
  assert.equal(authorizationUrl.searchParams.get("redirectUri"), env.CHZZK_REDIRECT_URI);
});

test("category search rejects requests without a broker token", async () => {
  const response = await worker.fetch(new Request("https://broker.example/categories?query=test"), env);
  assert.equal(response.status, 401);
});
