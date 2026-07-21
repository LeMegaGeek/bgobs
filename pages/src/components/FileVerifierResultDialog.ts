// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import { verifyFileResult } from "../lib/file-verification.mjs";

export interface FileVerifierReleaseAsset {
  name: string;
  size: number;
  digest: string | null;
}

export interface VerifiedResultItem {
  name: string;
  sha256?: string;
  size?: number;
  error?: unknown;
}

export class FileVerifierResultDialog extends HTMLElement {
  private releaseAssets: FileVerifierReleaseAsset[] = [];
  private dialog: HTMLDialogElement;
  private listElement: HTMLDListElement;

  constructor() {
    super();
    this.attachShadow({ mode: "open" });

    this.dialog = document.createElement("dialog");
    const style = document.createElement("style");
    style.textContent = `
      dialog {
        width: min(38rem, calc(100% - 2rem));
        max-height: calc(100vh - 2rem);
        padding: 1.25rem;
        overflow: auto;
        border: 1px solid #52627d;
        border-radius: 1rem;
        color: #f7f9fc;
        background: #151c2b;
        box-shadow: 0 24px 70px rgb(0 0 0 / 55%);
      }
      dialog::backdrop { background: rgb(0 0 0 / 72%); }
      h2 { margin-top: 0; }
      dt { margin-top: .8rem; font-weight: 750; overflow-wrap: anywhere; }
      dd { margin-left: 0; color: #dbe3ef; overflow-wrap: anywhere; }
      button {
        min-height: 2.6rem;
        padding: .55rem 1rem;
        border: 1px solid #ff8a1f;
        border-radius: .6rem;
        color: #271100;
        background: #ffad5c;
        cursor: pointer;
        font: inherit;
        font-weight: 750;
      }
      button:focus-visible { outline: 3px solid #55d9ee; outline-offset: 3px; }
    `;

    const h2 = document.createElement("h2");
    h2.textContent = "Verification Results";
    h2.id = "dialog-title";
    this.dialog.setAttribute("aria-labelledby", "dialog-title");
    this.dialog.appendChild(h2);

    this.listElement = document.createElement("dl");
    this.dialog.appendChild(this.listElement);

    const form = document.createElement("form");
    form.method = "dialog";
    form.style.textAlign = "right";

    const button = document.createElement("button");
    button.textContent = "Close";

    form.appendChild(button);
    this.dialog.appendChild(form);

    this.shadowRoot!.append(style, this.dialog);
  }

  connectedCallback() {
    const assetsAttr = this.getAttribute("data-release-assets");
    if (assetsAttr) {
      try {
        this.releaseAssets = JSON.parse(assetsAttr);
      } catch (e) {
        console.error("Failed to parse release assets JSON:", e);
      }
    }
  }

  public addResult(item: VerifiedResultItem) {
    const dt = document.createElement("dt");
    dt.textContent = item.name;
    dt.style.fontWeight = "bold";
    this.listElement.appendChild(dt);

    const verification = verifyFileResult(this.releaseAssets, item);
    if (verification.kind === "error") {
      const dd = document.createElement("dd");
      dd.textContent = `❌ Error: ${verification.message}`;
      this.listElement.appendChild(dd);
      return;
    }

    if (verification.kind === "official") {
      const expectedAsset = verification.asset;

      const ddName = document.createElement("dd");
      ddName.textContent = `✅ Official Name: ${expectedAsset.name}`;
      this.listElement.appendChild(ddName);

      const ddHash = document.createElement("dd");
      ddHash.textContent = `✅ SHA-256: ${verification.sha256}`;
      this.listElement.appendChild(ddHash);

      const ddSize = document.createElement("dd");
      if (verification.sizeMatches) {
        ddSize.textContent = `✅ Size: ${verification.actualSize} bytes`;
      } else {
        ddSize.textContent = `❌ Size: ${verification.actualSize} bytes (Expected: ${expectedAsset.size} bytes)`;
      }
      this.listElement.appendChild(ddSize);
    } else if (verification.kind === "unknown") {
      const ddUnknown = document.createElement("dd");
      ddUnknown.textContent = "❌ Unknown file (SHA-256 verification failed)";
      this.listElement.appendChild(ddUnknown);
    } else {
      const ddError = document.createElement("dd");
      ddError.textContent = "❌ Unknown Error (No hash provided)";
      this.listElement.appendChild(ddError);
    }
  }

  public showModal() {
    if (!this.dialog.open) this.dialog.showModal();
  }
}
