#pragma once

#include <vector>

/**
 * Generates a linear-phase FIR impulse response from a piecewise-linear
 * dB magnitude specification covering the full 0 Hz to Nyquist range.
 *
 * The caller provides arrays of frequencies (Hz) and corresponding gains (dB).
 * Linear interpolation is used to obtain the gain at each FFT bin.
 *
 * @param sampleRate   Sample rate in Hz, e.g. 48000.0.
 * @param freqPoints   Pointer to an array of frequency values (Hz). Must
 *                     be strictly ascending. It is sampled in a range between
 *                     0 Hz and above the Nyquist frequency (sampleRate/2).
 * @param gaindB       Pointer to an array of dB gains corresponding to
 *                     freqPoints. Same length as freqPoints.
 * @param numPoints    Number of entries in freqPoints, gaindB (>= 2).
 * @param kaiserBeta   Kaiser window shape parameter. 8.0 gives approx.
 *                     95 dB stop-band suppression, which is a high-quality
 *                     default for audio. Set to 0.0 for rectangular window
 *                     (testing only, not recommended).
 * @param irOut        Output array of impulse response in linear gain (not dB).
 * @param irLength     Size of irOut and the desired number of FIR taps (must be >= 2).
 *                     Longer IR -> finer frequency resolution but higher
 *                     latency (group delay = (irLength-1)/2 samples).
 * @param verify       If true, it will validate that the generated IR matches.
 */
void freq_to_ir(float sampleRate, const float* freqPoints, const float* gaindB, size_t numPoints,
    float kaiserBeta, float* irOut, size_t irLength, bool verify);
