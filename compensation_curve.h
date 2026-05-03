#pragma once

// Returns loudness compensation curve in dB for the given loudness pair, such that after correction
// the signal at `phon` sounds like it was played at reference_loudness
// The values are defined in a loudness range of 20 - 100 phon
// Frequencies are defined in a range from 20 - 20000 Hz, extended from the source dataset which only goes up to 12500 Hz
// Input frequencies must be monotonically increasing
// Outputs normalized per-frequency gain, ensuring it doesn't cross the clipping threshold
void make_compensation_curve(float reference_loudness, float loudness, float clipping_threshold, const float* freq_points, int freq_count, float* freq_gains);
