import test from "node:test";
import assert from "node:assert/strict";

import worker, { createSignedValue, verifySignedValue } from "../src/index.mjs";

globalThis.btoa ??= (value) => Buffer.from(value, "binary").toString("base64");
globalThis.atob ??= (value) => Buffer.from(value, "base64").toString("binary");

const secret = "test-secret-with-enough-entropy";
const env = {
  CHZZK_CLIENT_ID: "test-client",
  CHZZK_CLIENT_SECRET: "test-client-secret",
  CHZZK_REDIRECT_URI: "http://127.0.0.1:20132/callback",
  STATE_SECRET: secret,
};

test("signed values round trip", async () => {
  const value = await createSignedValue({ purpose: "oauth-state", exp: Date.now() + 10_000 }, secret);
  const payload = await verifySignedValue(value, secret, "oauth-state");
  assert.equal(payload.purpose, "oauth-state");
});

test("tampered values are rejected", async () => {
  const value = await createSignedValue({ purpose: "oauth-state", exp: Date.now() + 10_000 }, secret);
  const tampered = `${value.slice(0, -1)}${value.endsWith("a") ? "b" : "a"}`;
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
