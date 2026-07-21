// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import assert from "node:assert/strict";
import test from "node:test";
import { verifyFileResult } from "../src/lib/file-verification.mjs";

const assets = [{ name: "bgobs.zip", size: 42, digest: "sha256:abcd" }];

test("accepts a matching official asset", () => {
  const result = verifyFileResult(assets, {
    name: "download.zip",
    size: 42,
    sha256: "abcd",
  });
  assert.equal(result.kind, "official");
  assert.equal(result.sizeMatches, true);
  assert.equal(result.asset.name, "bgobs.zip");
});

test("distinguishes hash, size and processing failures", () => {
  assert.equal(
    verifyFileResult(assets, { name: "x", sha256: "bad" }).kind,
    "unknown",
  );
  assert.equal(verifyFileResult(assets, { name: "x" }).kind, "missing-hash");
  assert.equal(
    verifyFileResult(assets, { name: "x", error: new Error("read failed") })
      .kind,
    "error",
  );
  assert.equal(
    verifyFileResult(assets, { name: "x", size: 41, sha256: "abcd" })
      .sizeMatches,
    false,
  );
});
