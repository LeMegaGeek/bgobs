# CaCam USB streaming protocol

This document is the complete, sender-independent specification of version 1
of the byte stream consumed by BGOBS's optional CaCam source. A compatible
sender can be implemented from this document without access to the private
CaCam Android application or its source code.

BGOBS is open source. CaCam is a separate, privately developed Android app and
is only one implementation of the sender side of this protocol. Neither CaCam
nor this protocol is required for BGOBS's background and enhancement filters.

## Transport

Version 1 uses an ordered, reliable byte stream carried from Android to the
host over an Android Open Accessory (AOA) bulk **IN** endpoint. BGOBS acts as the
USB host and requests AOA mode with these identification strings:

| AOA index | Field        | Value                           |
| --------: | ------------ | ------------------------------- |
|         0 | manufacturer | `LeMegaGeek`                    |
|         1 | model        | `CaCam USB`                     |
|         2 | description  | `CaCam direct USB video source` |
|         3 | version      | `1.0`                           |
|         4 | URI          | `https://lemegageek.github.io/` |
|         5 | serial       | `CaCam`                         |

After Android re-enumerates in accessory mode, the sender opens the accessory
and writes messages to the stream. USB transfer boundaries have no message
semantics: a header or payload may be split across several transfers, and one
transfer may contain several messages. Receivers therefore parse the stream by
byte count, not by transfer count.

The wire protocol provides no encryption, authentication, or integrity tag.
Implementations should trust only a physically controlled USB connection and
must validate all lengths before allocating or decoding data.

## Integer encoding

All multibyte integers are unsigned and encoded in network byte order (big
endian). Offsets below are measured from the start of the enclosing header or
payload.

## Message header

Every message begins with this fixed 20-byte header:

| Offset | Size | Field            | Version 1 value                          |
| -----: | ---: | ---------------- | ---------------------------------------- |
|      0 |    4 | magic            | `43 43 41 4d` (`CCAM`)                   |
|      4 |    1 | protocol version | `01`                                     |
|      5 |    1 | message type     | see below                                |
|      6 |    2 | reserved         | sender writes zero; receiver ignores     |
|      8 |    4 | payload size     | bytes following the header               |
|     12 |    8 | send timestamp   | monotonic microseconds; zero for `HELLO` |

The maximum accepted payload is 16 MiB. A receiver must reject invalid magic,
an unsupported version, an oversized payload, or a truncated message. Unknown
message types are read using `payload size` and ignored, preserving stream
alignment for future optional message types.

## Message types

### Type 1: `HELLO`

The payload is a UTF-8 JSON object describing the sender. Version 1 senders use
these keys:

```json
{
  "app": "CaCam",
  "version": "1.2.3",
  "protocol": "cacam-usb-nv21",
  "stream": "nv21"
}
```

The values above are an example, not a required product identity or application
version. Receivers must ignore additional JSON keys. The message-header send
timestamp is zero for `HELLO`.

### Type 3: `NV21` frame

The payload starts with 16 bytes of frame metadata followed by tightly packed
NV21 data:

| Offset |                     Size | Field                                       |
| -----: | -----------------------: | ------------------------------------------- |
|      0 |                        4 | width in pixels                             |
|      4 |                        4 | height in pixels                            |
|      8 |                        8 | capture timestamp in monotonic microseconds |
|     16 | `width × height × 3 / 2` | NV21 image bytes                            |

Width and height must be in the range 1…4096 and must be even so the NV21
chroma plane is complete. The expected payload length is exactly
`16 + width × height × 3 / 2`; implementations must perform this calculation
with checked arithmetic before allocation.

NV21 stores the full-resolution Y plane first, followed by interleaved VU
chroma samples at half resolution in each dimension. The frame timestamp and
the header send timestamp use the sender's monotonic clock, not wall-clock time.
They may be used to estimate queue and transport delay but cannot be compared
directly with a host wall clock. A receiver may discard stale frames.

## State and compatibility

A sender should write one `HELLO` after opening the stream, then zero or more
`NV21` messages. Disconnecting ends the session; reconnecting starts a new
stream and requires another `HELLO`.

Existing fields must not change within protocol version 1. New optional message
types may be added without changing the version because unknown types are
length-delimited. A change to the header layout, integer encoding, or an
existing payload requires a new protocol version and coordinated contract tests.

## Canonical vectors

- Empty `HELLO` message:
  `43 43 41 4d 01 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00`
- NV21 metadata for 2×2 pixels at timestamp 1:
  `00 00 00 02 00 00 00 02 00 00 00 00 00 00 00 01`

These vectors test framing and endianness only. Implementations should also
cover partial reads, coalesced messages, unsupported versions, unknown message
types, checked dimension arithmetic, oversized payloads, and disconnects in the
middle of a header or payload.

<!--
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
