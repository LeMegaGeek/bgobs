// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

//! C ABI used by the native OBS plugin layer.

use std::slice;

use crate::mask::{
    write_background_mask_from_foreground, write_clean_background_mask,
    write_smooth_temporal_background_mask, MaskError, MaskPostProcessingSettings,
    SpatialMaskCleanupSettings, TemporalMaskSmoothingSettings,
};

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BgobsMaskPostProcessingSettings {
    pub enable_threshold: u8,
    pub threshold: f32,
    pub edge_softness: f32,
}

impl From<BgobsMaskPostProcessingSettings> for MaskPostProcessingSettings {
    fn from(settings: BgobsMaskPostProcessingSettings) -> Self {
        Self {
            enable_threshold: settings.enable_threshold != 0,
            threshold: settings.threshold,
            edge_softness: settings.edge_softness,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BgobsTemporalMaskSmoothingSettings {
    pub temporal_smooth_factor: f32,
    pub protect_foreground: u8,
}

impl From<BgobsTemporalMaskSmoothingSettings> for TemporalMaskSmoothingSettings {
    fn from(settings: BgobsTemporalMaskSmoothingSettings) -> Self {
        Self {
            temporal_smooth_factor: settings.temporal_smooth_factor,
            protect_foreground: settings.protect_foreground != 0,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BgobsSpatialMaskCleanupSettings {
    pub width: usize,
    pub height: usize,
    pub foreground_cleanup: f32,
    pub background_cleanup: f32,
}

impl From<BgobsSpatialMaskCleanupSettings> for SpatialMaskCleanupSettings {
    fn from(settings: BgobsSpatialMaskCleanupSettings) -> Self {
        Self {
            width: settings.width,
            height: settings.height,
            foreground_cleanup: settings.foreground_cleanup,
            background_cleanup: settings.background_cleanup,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BgobsMaskStatus {
    Ok = 0,
    NullPointer = 1,
    OutputLengthMismatch = 2,
    TemporalLengthMismatch = 3,
    SpatialLengthMismatch = 4,
}

#[no_mangle]
/// Writes a background mask into a C-owned output buffer.
///
/// # Safety
///
/// Non-null pointers must reference readable or writable buffers of their
/// matching length. Null pointers are accepted only when the matching length is
/// zero. Input and output buffers must not overlap mutably.
pub unsafe extern "C" fn bgobs_write_background_mask_from_foreground(
    foreground_mask: *const u8,
    foreground_len: usize,
    settings: BgobsMaskPostProcessingSettings,
    background_mask: *mut u8,
    background_len: usize,
) -> BgobsMaskStatus {
    let Some(foreground_mask) = (unsafe { const_slice_from_raw(foreground_mask, foreground_len) })
    else {
        return BgobsMaskStatus::NullPointer;
    };
    let Some(background_mask) = (unsafe { mut_slice_from_raw(background_mask, background_len) })
    else {
        return BgobsMaskStatus::NullPointer;
    };

    write_background_mask_from_foreground(foreground_mask, settings.into(), background_mask)
        .map_or_else(BgobsMaskStatus::from, |_| BgobsMaskStatus::Ok)
}

#[no_mangle]
/// Writes a temporally smoothed background mask into a C-owned output buffer.
///
/// # Safety
///
/// Non-null pointers must reference readable or writable buffers of their
/// matching length. Null pointers are accepted only when the matching length is
/// zero. The output buffer must not overlap mutably with either input buffer.
pub unsafe extern "C" fn bgobs_write_smooth_temporal_background_mask(
    current_background_mask: *const u8,
    current_len: usize,
    previous_background_mask: *const u8,
    previous_len: usize,
    settings: BgobsTemporalMaskSmoothingSettings,
    smoothed_mask: *mut u8,
    smoothed_len: usize,
) -> BgobsMaskStatus {
    let Some(current_background_mask) =
        (unsafe { const_slice_from_raw(current_background_mask, current_len) })
    else {
        return BgobsMaskStatus::NullPointer;
    };
    let Some(previous_background_mask) =
        (unsafe { const_slice_from_raw(previous_background_mask, previous_len) })
    else {
        return BgobsMaskStatus::NullPointer;
    };
    let Some(smoothed_mask) = (unsafe { mut_slice_from_raw(smoothed_mask, smoothed_len) }) else {
        return BgobsMaskStatus::NullPointer;
    };

    write_smooth_temporal_background_mask(
        current_background_mask,
        previous_background_mask,
        settings.into(),
        smoothed_mask,
    )
    .map_or_else(BgobsMaskStatus::from, |_| BgobsMaskStatus::Ok)
}

#[no_mangle]
/// Writes a spatially cleaned background mask into a C-owned output buffer.
///
/// # Safety
///
/// Non-null pointers must reference readable or writable buffers of their
/// matching length. Null pointers are accepted only when the matching length is
/// zero. Input and output buffers must not overlap mutably.
pub unsafe extern "C" fn bgobs_write_clean_background_mask(
    background_mask: *const u8,
    background_len: usize,
    settings: BgobsSpatialMaskCleanupSettings,
    cleaned_mask: *mut u8,
    cleaned_len: usize,
) -> BgobsMaskStatus {
    let Some(background_mask) = (unsafe { const_slice_from_raw(background_mask, background_len) })
    else {
        return BgobsMaskStatus::NullPointer;
    };
    let Some(cleaned_mask) = (unsafe { mut_slice_from_raw(cleaned_mask, cleaned_len) }) else {
        return BgobsMaskStatus::NullPointer;
    };

    write_clean_background_mask(background_mask, settings.into(), cleaned_mask)
        .map_or_else(BgobsMaskStatus::from, |_| BgobsMaskStatus::Ok)
}

impl From<MaskError> for BgobsMaskStatus {
    fn from(error: MaskError) -> Self {
        match error {
            MaskError::OutputLengthMismatch { .. } => BgobsMaskStatus::OutputLengthMismatch,
            MaskError::TemporalLengthMismatch { .. } => BgobsMaskStatus::TemporalLengthMismatch,
            MaskError::SpatialLengthMismatch { .. } => BgobsMaskStatus::SpatialLengthMismatch,
        }
    }
}

unsafe fn const_slice_from_raw<'a>(data: *const u8, len: usize) -> Option<&'a [u8]> {
    if len == 0 {
        return Some(&[]);
    }
    if data.is_null() {
        return None;
    }

    Some(unsafe { slice::from_raw_parts(data, len) })
}

unsafe fn mut_slice_from_raw<'a>(data: *mut u8, len: usize) -> Option<&'a mut [u8]> {
    if len == 0 {
        return Some(&mut []);
    }
    if data.is_null() {
        return None;
    }

    Some(unsafe { slice::from_raw_parts_mut(data, len) })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ptr;

    #[test]
    fn background_mask_ffi_writes_to_c_buffer() {
        let foreground = [0, 128, 255];
        let mut background = [0; 3];

        let status = unsafe {
            bgobs_write_background_mask_from_foreground(
                foreground.as_ptr(),
                foreground.len(),
                BgobsMaskPostProcessingSettings {
                    enable_threshold: 1,
                    threshold: 0.5,
                    edge_softness: 0.0,
                },
                background.as_mut_ptr(),
                background.len(),
            )
        };

        assert_eq!(status, BgobsMaskStatus::Ok);
        assert_eq!(background, [255, 0, 0]);
    }

    #[test]
    fn background_mask_ffi_reports_null_pointer() {
        let mut background = [0; 1];

        let status = unsafe {
            bgobs_write_background_mask_from_foreground(
                ptr::null(),
                1,
                BgobsMaskPostProcessingSettings {
                    enable_threshold: 1,
                    threshold: 0.5,
                    edge_softness: 0.0,
                },
                background.as_mut_ptr(),
                background.len(),
            )
        };

        assert_eq!(status, BgobsMaskStatus::NullPointer);
    }

    #[test]
    fn temporal_mask_ffi_accepts_empty_null_buffers() {
        let status = unsafe {
            bgobs_write_smooth_temporal_background_mask(
                ptr::null(),
                0,
                ptr::null(),
                0,
                BgobsTemporalMaskSmoothingSettings {
                    temporal_smooth_factor: 0.5,
                    protect_foreground: 1,
                },
                ptr::null_mut(),
                0,
            )
        };

        assert_eq!(status, BgobsMaskStatus::Ok);
    }

    #[test]
    fn spatial_cleanup_ffi_writes_to_c_buffer() {
        let mut background = [0; 7 * 7];
        background[3 * 7 + 3] = 255;
        let mut cleaned = [255; 7 * 7];

        let status = unsafe {
            bgobs_write_clean_background_mask(
                background.as_ptr(),
                background.len(),
                BgobsSpatialMaskCleanupSettings {
                    width: 7,
                    height: 7,
                    foreground_cleanup: 0.2,
                    background_cleanup: 0.0,
                },
                cleaned.as_mut_ptr(),
                cleaned.len(),
            )
        };

        assert_eq!(status, BgobsMaskStatus::Ok);
        assert_eq!(cleaned[3 * 7 + 3], 0);
    }
}
