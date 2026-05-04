#include "freq_to_ir.h"

#include <FFTConvolver/AudioFFT.h>
#include "../SDK/foobar2000.h"

#include <vector>
#define _USE_MATH_DEFINES
#include <math.h>
#include <cmath>
#include <algorithm>

float bessel_i0(float x)
{
    float sum = 1.0f, term = 1.0f;
    for (int k = 1; k <= 20; ++k) {
        term *= (x / (2.0f * k));
        term *= (x / (2.0f * k)); // (x/2)^(2k) / (k!)^2
        sum += term;
        if (term < 1e-6f * sum) break;
    }
    return sum;
}

void kaiser_window(float* w, size_t len, float beta)
{
    float denom = bessel_i0(beta);
    float half = 0.5f * static_cast<float>(len - 1);
    for (size_t i = 0; i < len; ++i) {
        float ratio = (static_cast<float>(i) - half) / half;
        float arg = beta * std::sqrt(max(0.0f, 1.0f - ratio * ratio));
        w[i] = bessel_i0(arg) / denom;
    }
}

bool verify_ir_response(
    float        sample_rate,
    const float* ir,
    size_t       ir_length,
    const float* freq_points,
    const float* gain_db,
    size_t       num_points,
    float        tolerance);

void freq_to_ir(float sample_rate, const float* freq_points, const float* gain_db, size_t num_points,
    float kaiser_beta, float* ir_out, size_t ir_length, bool verify)
{
    // Choose FFT size (oversampled >= 8x for negligible ripple)
    size_t fft_size = 1;
    while (fft_size < ir_length * 8) fft_size <<= 1;
    while (fft_size < 4096)          fft_size <<= 1; // decent minimum
    const size_t complex_size = audiofft::AudioFFT::ComplexSize(fft_size);

    // Linear interpolation of the dB gain table
    size_t fi = 0;
    auto gain_at_freq = [&](float freq) -> float {
        // Clamp to table limits
        if (freq <= freq_points[0]) return gain_db[0];
        if (freq >= freq_points[num_points - 1]) return gain_db[num_points - 1];

        // Find enclosing segment
        while (fi < num_points - 1 && freq > freq_points[fi + 1]) ++fi;
        float t = (freq - freq_points[fi]) /
            (freq_points[fi + 1] - freq_points[fi]);
        return gain_db[fi] + t * (gain_db[fi + 1] - gain_db[fi]);
        };

    // Build linear-phase complex spectrum
    std::vector<float> re(complex_size), im(complex_size);
    for (size_t k = 0; k < complex_size; ++k) {
        float freq = static_cast<float>(k) * sample_rate / static_cast<float>(fft_size);
        float mag_lin = std::pow(10.0f, gain_at_freq(freq) / 20.0f);

        if (k == 0 || k == complex_size - 1) { // DC & Nyquist are real
            re[k] = mag_lin;
            im[k] = 0.0f;
        } else {
            float phase = -M_PI * static_cast<float>(k)
                * static_cast<float>(ir_length - 1)
                / static_cast<float>(fft_size);
            re[k] = mag_lin * std::cos(phase);
            im[k] = mag_lin * std::sin(phase);
        }
    }

    // Inverse FFT
    audiofft::AudioFFT fft;
    fft.init(fft_size);
    std::vector<float> time_domain(fft_size);
    fft.ifft(time_domain.data(), re.data(), im.data());

    // Truncate to target length and apply Kaiser window
    std::vector<float> window(ir_length);
    if (kaiser_beta > 0.0f) {
        kaiser_window(window.data(), ir_length, kaiser_beta);
    } else {
        std::fill(window.begin(), window.end(), 1.0f);
    }

    for (size_t i = 0; i < ir_length; ++i) {
        ir_out[i] = time_domain[i] * window[i];
    }

    if (verify) {
        bool pass = verify_ir_response(sample_rate, ir_out, ir_length, freq_points, gain_db, num_points, 2.0);
        if (!pass) {
            FB2K_DebugLog() << "Generated IR Response does not match desired spectrum!";
        } else {
            FB2K_DebugLog() << "IR generated successfully. Length: " << ir_length;
        }
    }
}

bool verify_ir_response(float sample_rate, const float* ir, size_t ir_length, const float* freq_points,
    const float* gain_db, size_t num_points, float tolerance)
{
    // Choose a large FFT size for accurate frequency measurement
    size_t Nfft = 1;
    while (Nfft < ir_length * 16) Nfft <<= 1;   // at least 16x oversampling
    while (Nfft < 16384)          Nfft <<= 1;   // reasonable minimum
    const size_t complex_size = audiofft::AudioFFT::ComplexSize(Nfft);

    // Zero-pad the IR to fft_size
    std::vector<float> padded_ir(Nfft, 0.0f);
    for (size_t i = 0; i < ir_length; ++i)
        padded_ir[i] = ir[i];

    // Forward FFT (real-to-complex)
    audiofft::AudioFFT fft;
    fft.init(Nfft);
    std::vector<float> re(complex_size), im(complex_size);
    fft.fft(padded_ir.data(), re.data(), im.data());

    // Compute magnitude response in dB for each FFT bin
    std::vector<float> mag_db(complex_size);
    for (size_t k = 0; k < complex_size; ++k) {
        float mag = std::sqrt(re[k] * re[k] + im[k] * im[k]);
        // Avoid log of zero
        if (mag < 1e-10f)
            mag_db[k] = -200.0f;
        else
            mag_db[k] = 20.0f * std::log10(mag);
    }

    // Only check frequencies above the filter's resolution limit
    float min_verifiable_freq = (sample_rate / ir_length) * 4.0f;
    float max_verifiable_freq = sample_rate * 0.5f;

    // For each checked frequency, interpolate the measured response and compare
    float max_deviation = 0.0f;
    for (size_t i = 0; i < num_points; ++i) {
        float freq = freq_points[i];
        if (freq < 0.0f || freq > max_verifiable_freq)
            continue;

        // Map frequency to a continuous bin index
        float binPos = freq * static_cast<float>(Nfft) / sample_rate;
        size_t k0 = static_cast<size_t>(binPos);
        float frac = binPos - static_cast<float>(k0);

        // Linear interpolation between the two nearest FFT bins
        float measured_db;
        if (k0 + 1 >= complex_size) {
            // Nyquist or beyond, use the last bin directly
            measured_db = mag_db[complex_size - 1];
        } else {
            measured_db = mag_db[k0] * (1.0f - frac) + mag_db[k0 + 1] * frac;
        }

        float targetDB = gain_db[i];
        float deviation = std::fabs(measured_db - targetDB);

        if (deviation > tolerance) {
            FB2K_DebugLog() << "IR generation: Frequency response at " << pfc::format_float(freq, 0, 2) << " Hz"
                " exceeded threshold. Expected: " << pfc::format_float(targetDB, 0, 2) << " dB, measured: "
                << pfc::format_float(measured_db, 0, 2) << " dB.";
        }

        if (deviation > max_deviation)
            max_deviation = deviation;
    }

    FB2K_DebugLog() << "IR generation: Max deviation from target: " << pfc::format_float(max_deviation, 0, 2) << " dB";
    return max_deviation <= tolerance;
}
