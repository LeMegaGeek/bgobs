// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

//! Core processing algorithms for BGOBS.
//!
//! This crate deliberately has no OBS, OpenCV, or ONNX Runtime dependency. It is
//! the first Rust home for logic that can be tested independently, then called
//! from the native OBS plugin layer.

#![deny(unsafe_op_in_unsafe_fn)]

pub mod ffi;
pub mod mask;
