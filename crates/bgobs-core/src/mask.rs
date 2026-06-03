// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

//! Pure mask processing helpers.

/// Settings used to turn a foreground confidence mask into a background mask.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct MaskPostProcessingSettings {
    pub enable_threshold: bool,
    pub threshold: f32,
    pub edge_softness: f32,
}

impl Default for MaskPostProcessingSettings {
    fn default() -> Self {
        Self {
            enable_threshold: true,
            threshold: 0.5,
            edge_softness: 0.0,
        }
    }
}

/// Settings used to dampen frame-to-frame mask changes.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TemporalMaskSmoothingSettings {
    pub temporal_smooth_factor: f32,
    pub protect_foreground: bool,
}

impl Default for TemporalMaskSmoothingSettings {
    fn default() -> Self {
        Self {
            temporal_smooth_factor: 0.0,
            protect_foreground: true,
        }
    }
}

/// Errors returned when output buffers cannot hold a full mask.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MaskError {
    OutputLengthMismatch {
        input_len: usize,
        output_len: usize,
    },
    TemporalLengthMismatch {
        current_len: usize,
        previous_len: usize,
        output_len: usize,
    },
}

/// Builds a new background mask from an 8-bit foreground confidence mask.
pub fn background_mask_from_foreground(
    foreground_mask: &[u8],
    settings: MaskPostProcessingSettings,
) -> Vec<u8> {
    let mut background_mask = vec![0; foreground_mask.len()];
    write_background_mask_from_foreground(foreground_mask, settings, &mut background_mask)
        .expect("newly allocated output mask always matches the input length");
    background_mask
}

/// Writes a background mask into `background_mask`.
///
/// Input and output masks are 8-bit single-channel buffers. `foreground_mask`
/// stores foreground confidence: 0 means background, 255 means foreground.
/// `background_mask` stores the inverse alpha used by the compositor.
pub fn write_background_mask_from_foreground(
    foreground_mask: &[u8],
    settings: MaskPostProcessingSettings,
    background_mask: &mut [u8],
) -> Result<(), MaskError> {
    if foreground_mask.len() != background_mask.len() {
        return Err(MaskError::OutputLengthMismatch {
            input_len: foreground_mask.len(),
            output_len: background_mask.len(),
        });
    }

    if !settings.enable_threshold {
        for (foreground, background) in foreground_mask.iter().zip(background_mask) {
            *background = 255_u8.saturating_sub(*foreground);
        }
        return Ok(());
    }

    let threshold = settings.threshold.clamp(0.0, 1.0);
    let softness = settings.edge_softness.clamp(0.0, 1.0);

    if softness <= 0.0 {
        write_hard_background_mask(foreground_mask, threshold, background_mask);
        return Ok(());
    }

    let lower = (threshold - softness * 0.5).clamp(0.0, 1.0);
    let upper = (threshold + softness * 0.5).clamp(0.0, 1.0);
    if upper <= lower {
        write_hard_background_mask(foreground_mask, threshold, background_mask);
        return Ok(());
    }

    for (foreground, background) in foreground_mask.iter().zip(background_mask) {
        let foreground_alpha = f32::from(*foreground) / 255.0;
        let alpha = ((foreground_alpha - lower) / (upper - lower)).clamp(0.0, 1.0);
        let smooth_alpha = alpha * alpha * (3.0 - 2.0 * alpha);
        *background = ((1.0 - smooth_alpha) * 255.0).round().clamp(0.0, 255.0) as u8;
    }

    Ok(())
}

/// Builds a temporally smoothed background mask.
pub fn smooth_temporal_background_mask(
    current_background_mask: &[u8],
    previous_background_mask: &[u8],
    settings: TemporalMaskSmoothingSettings,
) -> Result<Vec<u8>, MaskError> {
    let mut smoothed_mask = vec![0; current_background_mask.len()];
    write_smooth_temporal_background_mask(
        current_background_mask,
        previous_background_mask,
        settings,
        &mut smoothed_mask,
    )?;
    Ok(smoothed_mask)
}

/// Writes a temporally smoothed background mask into `smoothed_mask`.
pub fn write_smooth_temporal_background_mask(
    current_background_mask: &[u8],
    previous_background_mask: &[u8],
    settings: TemporalMaskSmoothingSettings,
    smoothed_mask: &mut [u8],
) -> Result<(), MaskError> {
    if current_background_mask.len() != previous_background_mask.len()
        || current_background_mask.len() != smoothed_mask.len()
    {
        return Err(MaskError::TemporalLengthMismatch {
            current_len: current_background_mask.len(),
            previous_len: previous_background_mask.len(),
            output_len: smoothed_mask.len(),
        });
    }

    let factor = settings.temporal_smooth_factor.clamp(0.0, 1.0);
    if factor <= 0.0 || factor >= 1.0 {
        smoothed_mask.copy_from_slice(current_background_mask);
        return Ok(());
    }

    let recover_foreground_factor = if settings.protect_foreground {
        (0.65 + 0.35 * factor).clamp(factor, 0.98)
    } else {
        factor
    };
    let lose_foreground_factor = if settings.protect_foreground {
        (0.35 * factor).clamp(0.05, factor)
    } else {
        factor
    };

    for ((current, previous), smoothed) in current_background_mask
        .iter()
        .zip(previous_background_mask)
        .zip(smoothed_mask)
    {
        let update_factor = if current > previous {
            lose_foreground_factor
        } else {
            recover_foreground_factor
        };
        let blended =
            f32::from(*current) * update_factor + f32::from(*previous) * (1.0 - update_factor);
        *smoothed = blended.round().clamp(0.0, 255.0) as u8;
    }

    Ok(())
}

fn write_hard_background_mask(foreground_mask: &[u8], threshold: f32, background_mask: &mut [u8]) {
    let threshold_value = (threshold.clamp(0.0, 1.0) * 255.0) as u8;
    for (foreground, background) in foreground_mask.iter().zip(background_mask) {
        *background = if *foreground < threshold_value {
            255
        } else {
            0
        };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hard_threshold_matches_current_plugin_boundary() {
        let foreground = [0, 126, 127, 128, 255];
        let background = background_mask_from_foreground(
            &foreground,
            MaskPostProcessingSettings {
                threshold: 0.5,
                ..Default::default()
            },
        );

        assert_eq!(background, [255, 255, 0, 0, 0]);
    }

    #[test]
    fn disabled_threshold_inverts_foreground_confidence() {
        let foreground = [0, 64, 255];
        let background = background_mask_from_foreground(
            &foreground,
            MaskPostProcessingSettings {
                enable_threshold: false,
                ..Default::default()
            },
        );

        assert_eq!(background, [255, 191, 0]);
    }

    #[test]
    fn soft_threshold_keeps_fractional_edges() {
        let foreground = [0, 128, 255];
        let background = background_mask_from_foreground(
            &foreground,
            MaskPostProcessingSettings {
                threshold: 0.5,
                edge_softness: 1.0,
                ..Default::default()
            },
        );

        assert_eq!(background[0], 255);
        assert!((124..=130).contains(&background[1]));
        assert_eq!(background[2], 0);
    }

    #[test]
    fn reports_output_length_mismatch() {
        let mut background = [0; 1];
        let error = write_background_mask_from_foreground(
            &[0, 255],
            MaskPostProcessingSettings::default(),
            &mut background,
        )
        .unwrap_err();

        assert_eq!(
            error,
            MaskError::OutputLengthMismatch {
                input_len: 2,
                output_len: 1
            }
        );
    }

    #[test]
    fn temporal_smoothing_can_protect_foreground_edges() {
        let smoothed = smooth_temporal_background_mask(
            &[200, 50],
            &[100, 150],
            TemporalMaskSmoothingSettings {
                temporal_smooth_factor: 0.5,
                protect_foreground: true,
            },
        )
        .unwrap();

        assert_eq!(smoothed, [118, 68]);
    }

    #[test]
    fn temporal_smoothing_without_protection_uses_same_factor_both_ways() {
        let smoothed = smooth_temporal_background_mask(
            &[200, 50],
            &[100, 150],
            TemporalMaskSmoothingSettings {
                temporal_smooth_factor: 0.5,
                protect_foreground: false,
            },
        )
        .unwrap();

        assert_eq!(smoothed, [150, 100]);
    }
}
