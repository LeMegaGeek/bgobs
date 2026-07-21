# CaCam USB protocol

> SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>  
> SPDX-License-Identifier: GPL-3.0-or-later

The authoritative version 1 wire specification is maintained with the CaCam
Android sender in `docs/CACAM-USB-PROTOCOL.md` of the CaCam repository.

BGOBS implements the receiver in `src/cacam-usb-source.cpp`. Its compatibility
contract is:

- fixed 20-byte, big-endian `CCAM` header;
- version `1`, message type `1` for UTF-8 JSON `HELLO`, type `3` for NV21;
- NV21 metadata is big-endian width, height and capture timestamp (16 bytes);
- maximum payload 16 MiB and maximum frame dimensions 4096×4096;
- unknown message types are consumed and ignored;
- unsupported versions and malformed headers are rejected.

Canonical contract vectors:

- Empty `HELLO`: `43 43 41 4d 01 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00`
- NV21 metadata for 2×2 at timestamp 1:
  `00 00 00 02 00 00 00 02 00 00 00 00 00 00 00 01`

Any incompatible layout change requires a new protocol version and coordinated
sender/receiver tests.
