#pragma once

#include <vector>

/**
 * Generates a linear-phase FIR impulse response from a piecewise-linear
 * dB magnitude specification covering the full 0 Hz to Nyquist range.
 *
 * The caller provides arrays of frequencies (Hz) and corresponding gains (dB).
 * Linear interpolation is used to obtain the gain at each FFT bin.
 *
 * @param sample_rate  Sample rate in Hz, e.g. 48000.0.
 * @param freq_points  Pointer to an array of frequency values (Hz). Must
 *                     be strictly ascending. It is sampled in a range between
 *                     0 Hz and above the Nyquist frequency (sample_rate/2).
 * @param gain_db      Pointer to an array of dB gains corresponding to
 *                     freq_points. Same length as freq_points.
 * @param num_points   Number of entries in freq_points, gain_db (>= 2).
 * @param kaiser_beta  Kaiser window shape parameter. 8.0 gives approx.
 *                     95 dB stop-band suppression, which is a high-quality
 *                     default for audio. Set to 0.0 for rectangular window
 *                     (testing only, not recommended).
 * @param ir_out       Output array of impulse response in linear gain (not dB).
 * @param ir_length    Size of ir_out and the desired number of FIR taps (must be >= 2).
 *                     Longer IR -> finer frequency resolution but higher
 *                     latency (group delay = (ir_length-1)/2 samples).
 * @param verify       If true, it will validate that the generated IR matches.
 */
void freq_to_ir(float sample_rate, const float* freq_points, const float* gain_db, size_t num_points,
    float kaiser_beta, float* ir_out, size_t ir_length, bool verify);
