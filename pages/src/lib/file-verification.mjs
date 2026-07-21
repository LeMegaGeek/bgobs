// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
// SPDX-License-Identifier: GPL-3.0-or-later

export function verifyFileResult(releaseAssets, item) {
  if (item.error) {
    return {
      kind: "error",
      message:
        item.error instanceof Error ? item.error.message : String(item.error),
    };
  }
  if (!item.sha256) return { kind: "missing-hash" };
  const asset = releaseAssets.find(
    (candidate) => candidate.digest === `sha256:${item.sha256}`,
  );
  if (!asset) return { kind: "unknown", sha256: item.sha256 };
  return {
    kind: "official",
    asset,
    sha256: item.sha256,
    actualSize: item.size,
    sizeMatches: asset.size === item.size,
  };
}
