#include "freq_to_ir.h"

#include <FFTConvolver/AudioFFT.h>
#include "../SDK/foobar2000.h"

#include <vector>
#define _USE_MATH_DEFINES
#include <math.h>
#include <cmath>
#include <algorithm>

float besselI0(float x)
{
    float sum = 1.0f, term = 1.0f;
    for (int k = 1; k <= 20; ++k) {
        term *= (x / (2.0f * k));
        term *= (x / (2.0f * k));   // (x/2)^(2k) / (k!)^2
        sum += term;
        if (term < 1e-6f * sum) break;
    }
    return sum;
}

void kaiserWindow(float* w, size_t len, float beta)
{
    float denom = besselI0(beta);
    float half = 0.5f * static_cast<float>(len - 1);
    for (size_t i = 0; i < len; ++i) {
        float ratio = (static_cast<float>(i) - half) / half;
        float arg = beta * std::sqrt(max(0.0f, 1.0f - ratio * ratio));
        w[i] = besselI0(arg) / denom;
    }
}

bool verify_ir_response(
    float        sampleRate,
    const float* ir,
    size_t       irLength,
    const float* freqPoints,
    const float* gaindB,
    size_t       numPoints,
    float        tolerance);

void freq_to_ir(float sampleRate, const float* freqPoints, const float* gaindB, size_t numPoints,
    float kaiserBeta, float* irOut, size_t irLength, bool verify)
{
    // --- Choose FFT size (oversampled >= 8x for negligible ripple) ---
    size_t Nfft = 1;
    while (Nfft < irLength * 8) Nfft <<= 1;
    while (Nfft < 4096)         Nfft <<= 1; // decent minimum
    const size_t complexSize = audiofft::AudioFFT::ComplexSize(Nfft); // Nfft/2+1

    // --- Linear interpolation of the dB gain table ---
    size_t fi = 0;
    auto dbAtFreq = [&](float freq) -> float {
        // Clamp to table limits
        if (freq <= freqPoints[0]) return gaindB[0];
        if (freq >= freqPoints[numPoints - 1]) return gaindB[numPoints - 1];

        // Find enclosing segment
        while (fi < numPoints - 1 && freq > freqPoints[fi + 1]) ++fi;
        float t = (freq - freqPoints[fi]) /
            (freqPoints[fi + 1] - freqPoints[fi]);
        return gaindB[fi] + t * (gaindB[fi + 1] - gaindB[fi]);
        };

    // --- Build linear-phase complex spectrum ---
    std::vector<float> re(complexSize), im(complexSize);
    for (size_t k = 0; k < complexSize; ++k) {
        float freq = static_cast<float>(k) * sampleRate / static_cast<float>(Nfft);
        float magLin = std::pow(10.0f, dbAtFreq(freq) / 20.0f);

        if (k == 0 || k == complexSize - 1) { // DC & Nyquist are real
            re[k] = magLin;
            im[k] = 0.0f;
        } else {
            float phase = -M_PI * static_cast<float>(k)
                * static_cast<float>(irLength - 1)
                / static_cast<float>(Nfft);
            re[k] = magLin * std::cos(phase);
            im[k] = magLin * std::sin(phase);
        }
    }

    // --- Inverse FFT ---
    audiofft::AudioFFT fft;
    fft.init(Nfft);
    std::vector<float> timeDomain(Nfft);
    fft.ifft(timeDomain.data(), re.data(), im.data());

    // --- Truncate to target length and apply Kaiser window ---
    std::vector<float> window(irLength);
    if (kaiserBeta > 0.0f) {
        kaiserWindow(window.data(), irLength, kaiserBeta);
    } else {
        std::fill(window.begin(), window.end(), 1.0f);
    }

    for (size_t i = 0; i < irLength; ++i) {
        irOut[i] = timeDomain[i] * window[i];
    }

    if (verify) {
        bool pass = verify_ir_response(sampleRate, irOut, irLength, freqPoints, gaindB, numPoints, 1.0);
        if (!pass) {
            fb2k::crashWithMessage("Generated IR Response does not match desired spectrum.");
        } else {
            FB2K_DebugLog() << "IR generated successfully. Length: " << irLength;
        }
    }
}

bool verify_ir_response(
    float        sampleRate,
    const float* ir,
    size_t       irLength,
    const float* freqPoints,
    const float* gaindB,
    size_t       numPoints,
    float        tolerance)
{
    // Choose a large FFT size for accurate frequency measurement
    size_t Nfft = 1;
    while (Nfft < irLength * 16)  Nfft <<= 1;   // at least 16x oversampling
    while (Nfft < 16384)          Nfft <<= 1;   // reasonable minimum
    const size_t complexSize = audiofft::AudioFFT::ComplexSize(Nfft);

    // Zero-pad the IR to Nfft
    std::vector<float> paddedIr(Nfft, 0.0f);
    for (size_t i = 0; i < irLength; ++i)
        paddedIr[i] = ir[i];

    // Forward FFT (real-to-complex)
    audiofft::AudioFFT fft;
    fft.init(Nfft);
    std::vector<float> re(complexSize), im(complexSize);
    fft.fft(paddedIr.data(), re.data(), im.data());

    // Compute magnitude response in dB for each FFT bin
    std::vector<float> magdB(complexSize);
    for (size_t k = 0; k < complexSize; ++k) {
        float mag = std::sqrt(re[k] * re[k] + im[k] * im[k]);
        // Avoid log of zero
        if (mag < 1e-10f)
            magdB[k] = -200.0f;
        else
            magdB[k] = 20.0f * std::log10(mag);
    }

    // For each check frequency, interpolate the measured response and compare
    float maxDeviation = 0.0f;
    for (size_t i = 0; i < numPoints; ++i) {
        float freq = freqPoints[i];
        if (freq < 0.0f || freq > sampleRate * 0.5f)
            continue;   // skip out-of-range frequencies

        // Map frequency to a continuous bin index
        float binPos = freq * static_cast<float>(Nfft) / sampleRate;
        size_t k0 = static_cast<size_t>(binPos);
        float frac = binPos - static_cast<float>(k0);

        // Linear interpolation between the two nearest FFT bins
        float measuredDB;
        if (k0 + 1 >= complexSize) {
            // Nyquist or beyond, use the last bin directly
            measuredDB = magdB[complexSize - 1];
        } else {
            measuredDB = magdB[k0] * (1.0f - frac) + magdB[k0 + 1] * frac;
        }

        float targetDB = gaindB[i];
        float deviation = std::fabs(measuredDB - targetDB);

        if (deviation > tolerance) {
            FB2K_DebugLog() << "IR generation: Frequency response at " << pfc::format_float(freq, 0, 2) << " Hz"
                " exceeded threshold. Expected: " << pfc::format_float(targetDB, 0, 2) << " dB, measured: "
                << pfc::format_float(measuredDB, 0, 2) << " dB.";
        }

        if (deviation > maxDeviation)
            maxDeviation = deviation;
    }

    return maxDeviation <= tolerance;
}
