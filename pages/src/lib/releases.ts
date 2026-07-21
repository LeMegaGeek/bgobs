// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import { Octokit } from "@octokit/rest";

import { GITHUB_OWNER, GITHUB_REPO } from "./info.js";

export type Release = Awaited<
  ReturnType<Octokit["rest"]["repos"]["getLatestRelease"]>
>["data"];
export type ReleaseAsset = Release["assets"][number];

export const ASSET_PATTERNS = {
  windows: /^bgobs-[\w.-]+-windows-x64-unsigned\.zip$/i,
  windowsSymbols: /^bgobs-[\w.-]+-windows-x64-debug-symbols\.zip$/i,
  macos: /^bgobs-[\w.-]+-macos-universal(?:-unsigned)?\.pkg$/i,
  macosUnsigned: /^bgobs-[\w.-]+-macos-universal-unsigned\.pkg$/i,
  ubuntu: /^bgobs-[\w.-]+-linux-x86_64-unsigned\.deb$/i,
  ubuntuSymbols: /^bgobs-[\w.-]+-linux-x86_64-debug-symbols\.ddeb$/i,
  checksums: /^SHA256SUMS-[\w.-]+\.txt$/i,
} as const;

let releasesPromise: Promise<Release[]> | undefined;

// v0.3.8 is the first release on the current public BGOBS release line.
// Earlier releases may carry transitional assets or notes from the private
// development period and must not be presented as current BGOBS packages.
const PUBLIC_BGOBS_RELEASE_EPOCH = Date.parse("2026-06-23T00:00:00Z");

export function listBgobsReleases(): Promise<Release[]> {
  if (!releasesPromise) {
    const octokit = new Octokit({ auth: import.meta.env.GITHUB_TOKEN });
    releasesPromise = octokit
      .paginate(octokit.rest.repos.listReleases, {
        owner: GITHUB_OWNER,
        repo: GITHUB_REPO,
        per_page: 100,
      })
      .then((releases) =>
        releases.filter(
          (release) =>
            !release.draft &&
            release.published_at &&
            Date.parse(release.published_at) >= PUBLIC_BGOBS_RELEASE_EPOCH &&
            /^v\d+\.\d+\.\d+$/.test(release.tag_name) &&
            release.assets.some((asset) => asset.name.startsWith("bgobs-")),
        ),
      );
  }

  return releasesPromise;
}

export async function getLatestBgobsRelease(): Promise<Release> {
  const releases = await listBgobsReleases();
  const latest = releases[0];
  if (!latest) {
    throw new Error("No published BGOBS release with official assets found.");
  }
  return latest;
}

export function findAsset(
  release: Release,
  pattern: RegExp,
): ReleaseAsset | undefined {
  return release.assets.find((asset) => pattern.test(asset.name));
}

export function findRuntimeAssets(release: Release): ReleaseAsset[] {
  return release.assets.filter((asset) =>
    [ASSET_PATTERNS.windows, ASSET_PATTERNS.macos, ASSET_PATTERNS.ubuntu].some(
      (pattern) => pattern.test(asset.name),
    ),
  );
}
