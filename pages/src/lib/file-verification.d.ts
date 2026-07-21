// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import type {
  FileVerifierReleaseAsset,
  VerifiedResultItem,
} from "../components/FileVerifierResultDialog";

export type FileVerification =
  | { kind: "error"; message: string }
  | { kind: "missing-hash" }
  | { kind: "unknown"; sha256: string }
  | {
      kind: "official";
      asset: FileVerifierReleaseAsset;
      sha256: string;
      actualSize: number | undefined;
      sizeMatches: boolean;
    };

export function verifyFileResult(
  releaseAssets: FileVerifierReleaseAsset[],
  item: VerifiedResultItem,
): FileVerification;
