// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

class FileVerifierDropAreaElement extends HTMLElement {
  private readonly label: HTMLLabelElement;
  private readonly input: HTMLInputElement;
  private controller?: AbortController;

  static sheet = new CSSStyleSheet();

  static {
    this.sheet.replaceSync(/*css*/ `
      :host {
        display: block;
        margin: 1rem 0;
      }

      label {
        position: relative;
        display: flex;
        min-height: 8rem;
        align-items: center;
        justify-content: center;
        flex-direction: column;
        padding: 1rem;
        border: 2px dashed #52627d;
        border-radius: 1rem;
        color: #dbe3ef;
        background: #090d16;
        cursor: pointer;
        text-align: center;
        transition:
          border-color 120ms ease,
          background 120ms ease,
          transform 120ms ease;
      }

      label:hover,
      label:focus-within {
        border-color: #ffad5c;
        background: #111827;
      }

      label.active {
        border-color: #55d9ee;
        background: #0b2630;
        transform: scale(1.01);
      }

      input {
        position: absolute;
        width: 1px;
        height: 1px;
        margin: -1px;
        padding: 0;
        overflow: hidden;
        border: 0;
        clip: rect(0 0 0 0);
        clip-path: inset(50%);
        white-space: nowrap;
      }

      input:focus-visible + span {
        outline: 3px solid #ffad5c;
        outline-offset: 5px;
      }

      strong {
        display: block;
        font-size: 1.05rem;
      }

      small {
        display: block;
        margin-top: 0.35rem;
        color: #b8c2d4;
      }

      @media (prefers-reduced-motion: reduce) {
        label {
          transition: none;
        }
      }
    `);
  }

  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    this.shadowRoot!.adoptedStyleSheets = [FileVerifierDropAreaElement.sheet];

    this.label = document.createElement("label");
    this.input = document.createElement("input");
    this.input.type = "file";
    this.input.multiple = true;
    this.input.setAttribute(
      "aria-describedby",
      "file-verifier-privacy-description",
    );

    const copy = document.createElement("span");
    const title = document.createElement("strong");
    title.textContent = "Choose files or drop them here";
    const description = document.createElement("small");
    description.id = "file-verifier-privacy-description";
    description.textContent =
      "SHA-256 is calculated in this browser. Files are never uploaded.";
    copy.append(title, description);
    this.label.append(this.input, copy);
    this.shadowRoot!.append(this.label);
  }

  connectedCallback() {
    this.controller = new AbortController();
    const { signal } = this.controller;

    this.input.addEventListener(
      "change",
      () => {
        if (this.input.files) void this.processFiles(this.input.files);
        this.input.value = "";
      },
      { signal },
    );

    this.label.addEventListener(
      "dragover",
      (event) => {
        event.preventDefault();
        this.label.classList.add("active");
      },
      { signal },
    );

    this.label.addEventListener(
      "dragleave",
      (event) => {
        event.preventDefault();
        this.label.classList.remove("active");
      },
      { signal },
    );

    this.label.addEventListener(
      "drop",
      (event: DragEvent) => {
        event.preventDefault();
        this.label.classList.remove("active");
        if (event.dataTransfer)
          void this.processFiles(event.dataTransfer.files);
      },
      { signal },
    );
  }

  disconnectedCallback() {
    this.controller?.abort();
  }

  private async processFiles(files: FileList) {
    for (const file of files) {
      try {
        const buffer = await file.arrayBuffer();
        const sha256Buffer = await crypto.subtle.digest("SHA-256", buffer);
        this.dispatchEvent(
          new CustomEvent("file-verifier-file-dropped", {
            detail: {
              name: file.name,
              size: file.size,
              sha256Buffer,
            },
            bubbles: true,
            composed: true,
          }),
        );
      } catch (error) {
        this.dispatchEvent(
          new CustomEvent("file-verifier-error", {
            detail: {
              name: file.name,
              size: file.size,
              error,
            },
            bubbles: true,
            composed: true,
          }),
        );
      }
    }
  }
}

if (!customElements.get("file-verifier-drop-area")) {
  customElements.define("file-verifier-drop-area", FileVerifierDropAreaElement);
}
