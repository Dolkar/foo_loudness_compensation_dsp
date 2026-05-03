#include "compensation_curve.h"

#include <cmath>

struct iso226_contour_sample
{
    // Frequency [Hz]
    float freq;
    // Loudness perception exponent
    float a_f;
    // Magnitude of the linear transfer function normalized at 1 000 Hz [dB]
    float L_U;
    // Threshold of hearing [dB]
    float T_f;
};

// Taken from ISO 226:2023
static const int contour_count = 31;
static const iso226_contour_sample contour[contour_count] = {
    {    20.0, 0.635, -31.5, 78.1 },
    {    25.0, 0.602, -27.2, 68.7 },
    {    31.5, 0.569, -23.1, 59.5 },
    {    40.0, 0.537, -19.3, 51.1 },
    {    50.0, 0.509, -16.1, 44.0 },
    {    63.0, 0.482, -13.1, 37.5 },
    {    80.0, 0.456, -10.4, 31.5 },
    {   100.0, 0.433,  -8.2, 26.5 },
    {   125.0, 0.412,  -6.3, 22.1 },
    {   160.0, 0.391,  -4.6, 17.9 },
    {   200.0, 0.373,  -3.2, 14.4 },
    {   250.0, 0.357,  -2.1, 11.4 },
    {   315.0, 0.343,  -1.2,  8.6 },
    {   400.0, 0.330,  -0.5,  6.2 },
    {   500.0, 0.320,   0.0,  4.4 },
    {   630.0, 0.311,   0.4,  3.0 },
    {   800.0, 0.303,   0.5,  2.2 },
    {  1000.0, 0.300,   0.0,  2.4 },
    {  1250.0, 0.295,  -2.7,  3.5 },
    {  1600.0, 0.292,  -4.2,  1.7 },
    {  2000.0, 0.290,  -1.2, -1.3 },
    {  2500.0, 0.290,   1.4, -4.2 },
    {  3150.0, 0.289,   2.3, -6.0 },
    {  4000.0, 0.289,   1.0, -5.4 },
    {  5000.0, 0.289,  -2.3, -1.5 },
    {  6300.0, 0.293,  -7.2,  6.0 },
    {  8000.0, 0.303, -11.2, 12.6 },
    { 10000.0, 0.323, -10.9, 13.9 },
    { 12500.0, 0.354,  -3.5, 12.3 },
    // Extended samples up to 20khz. Threshold of hearing at 16khz taken from ISO-389-7
    // The rest is chosen empirically for smooth results
    { 16000.0, 0.400, -10.0, 40.2 },
    { 20000.0, 0.500, -25.0, 80.0 }
};

// Helper: cubic Catmull-Rom interpolation for one parameter
// Given four points (x0,y0), (x1,y1), (x2,y2), (x3,y3) and a query x in [x1, x2],
// returns the interpolated y value.
float catmull_rom(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float x)
{
    float t = (x - x1) / (x2 - x1); // t in [0,1]
    float t2 = t * t;
    float t3 = t2 * t;

    // Catmull-Rom basis
    float y = 0.5f * ((2.0f * y1) +
        (-y0 + y2) * t +
        (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3) * t2 +
        (-y0 + 3.0f * y1 - 3.0f * y2 + y3) * t3);
    return y;
}

iso226_contour_sample interpolate_contour(int lower_bound_index, float frequency)
{
    int n = contour_count;
    int i = lower_bound_index;

    // Clamp frequency to the table range (optional)
    if (frequency <= contour[0].freq)
        return contour[0];
    if (frequency >= contour[n - 1].freq)
        return contour[n - 1];

    // Gather four points needed for cubic interpolation.
    // Indices: i-1, i, i+1, i+2. Duplicate at boundaries.
    int i0 = (i > 0) ? i - 1 : i;
    int i1 = i;
    int i2 = i + 1;
    int i3 = (i + 2 < n) ? i + 2 : i + 1;

    // Logarithmic x?coordinates
    float log_freq = logf(frequency);
    float x0 = logf(contour[i0].freq);
    float x1 = logf(contour[i1].freq);
    float x2 = logf(contour[i2].freq);
    float x3 = logf(contour[i3].freq);

    // Interpolate each parameter independently
    float a_f = catmull_rom(
        x0, contour[i0].a_f,
        x1, contour[i1].a_f,
        x2, contour[i2].a_f,
        x3, contour[i3].a_f,
        log_freq);

    float L_U = catmull_rom(
        x0, contour[i0].L_U,
        x1, contour[i1].L_U,
        x2, contour[i2].L_U,
        x3, contour[i3].L_U,
        log_freq);

    float T_f = catmull_rom(
        x0, contour[i0].T_f,
        x1, contour[i1].T_f,
        x2, contour[i2].T_f,
        x3, contour[i3].T_f,
        log_freq);

    return { frequency, a_f, L_U, T_f };
}

// Returns the sound pressure level in dB that corresponds to the given loudness at the frequency described by the contour sample
float evaluate_contour(const iso226_contour_sample& sample, float loudness)
{
    float a = powf(4e-10f, 0.3f - sample.a_f);
    float b = powf(10.0f, 0.03f * loudness) - powf(10.0f, 0.072f);
    float c = powf(10.0f, sample.a_f * (sample.T_f + sample.L_U) / 10.0f);

    return (10.0f / sample.a_f) * log10f(a * b + c) - sample.L_U;
}

// Evaluates the curve for the given parameters
void eval_curve(float reference_loudness, float loudness, const float* freq_points, int freq_count, float* freq_gains)
{
    int left = 0;
    for (int i = 0; i < freq_count; i++) {
        float freq = freq_points[i];
        freq = fmaxf(fminf(freq, 19999.0f), 20.0f);

        // Advance so that left stays the largest frequency smaller than freq
        for (; left < contour_count - 1; left++) {
            if (freq < contour[left + 1].freq)
                break;
        }

        iso226_contour_sample contour = interpolate_contour(left, freq);
        float current_spl = evaluate_contour(contour, loudness);
        float reference_spl = evaluate_contour(contour, reference_loudness);

        // Loudness = spl at 1khz, use it to keep that frequency unchanged
        freq_gains[i] = (current_spl - reference_spl) + (reference_loudness - loudness);
    }
}

// Returns the A-weighting linear multiplier for a given frequency
float a_weighting_factor(float freq)
{
    float f2 = freq * freq;

    // Numerator: 12200^2 * f^4
    float num = (12200.0 * 12200.0) * (f2 * f2);

    // Denominator components
    float den1 = (f2 + 20.6 * 20.6);
    float den2 = sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9));
    float den3 = (f2 + 12200.0 * 12200.0);

    float den = den1 * den2 * den3;

    // R_A(f) without normalization
    float ra = num / den;

    // Convert to dB and add normalization offset (+2.00 dB at 1 kHz)
    float a_db = 20.0 * log10(ra) + 2.00;

    // Convert dB to linear multiplier
    return pow(10.0, a_db / 20.0);
}

// Calculates the weighted average gain and maximum gain
void analyze_curve(const float* freq_points, const float* freq_gains, int freq_count, float& wavg_gain, float& max_gain)
{
    float w = 0.0f;
    wavg_gain = 0.0f;
    max_gain = 0.0f;
    for (int i = 0; i < freq_count; i++) {
        float freq_weight = a_weighting_factor(freq_points[i]);
        w += freq_weight;

        float gain = freq_gains[i];
        wavg_gain += gain * freq_weight;
        if (gain > max_gain) max_gain = gain;
    }
    wavg_gain /= w;
}

void make_compensation_curve(float reference_loudness, float loudness, float clipping_threshold, const float* freq_points, int freq_count, float* freq_gains)
{
    // First evaluate the curve directly on the given parameters
    eval_curve(reference_loudness, loudness, freq_points, freq_count, freq_gains);

    // We want to normalize it and also keep it below the clipping threshold
    float wavg_gain, max_gain;
    analyze_curve(freq_points, freq_gains, freq_count, wavg_gain, max_gain);

    if (max_gain - wavg_gain > clipping_threshold)
    {
        // We need to reduce loudness to prevent clipping. That requires us to re-evaluate
        loudness += clipping_threshold - (max_gain - wavg_gain);
        eval_curve(reference_loudness, loudness, freq_points, freq_count, freq_gains);
        analyze_curve(freq_points, freq_gains, freq_count, wavg_gain, max_gain);
    }

    // Normalize to not affect the current loudness, + clip with new levels
    float gain_adjust = fmin(-wavg_gain, fmin(clipping_threshold - max_gain, 0.0f));
    for (int i = 0; i < freq_count; i++) {
        freq_gains[i] += gain_adjust;
    }
}

#ifdef DEBUG_EXPORT_CURVES
#include <cstdio>
#include <vector>

void export_curves()
{
    int freq_count = 300;
    float start_freq = 20.0f;
    float end_freq = 20000.0f;

    std::vector<float> frequencies;
    frequencies.resize(freq_count);

    for (int i = 0; i < freq_count; i++) {
        float log_freq = (logf(end_freq) - logf(start_freq)) * i / (freq_count - 1.0f) + logf(start_freq);
        float freq = expf(log_freq);
        frequencies[i] = freq;
    }


    std::vector<float> freq_gains[7];
    float target_loudnesses[7] = { 20.0f, 40.0f, 60.0f, 70.0f, 80.0f, 90.0f, 100.0f };
    for (int i = 0; i < 7; i++) {
        freq_gains[i].resize(freq_count);
        eval_curve(80.0f, target_loudnesses[i], frequencies.data(), freq_count, freq_gains[i].data());
    }

    for (int i = 0; i < freq_count; i++) {
        printf("%6.1f, %6.2f, %6.2f, %6.2f, %6.2f, %6.2f, %6.2f, %6.2f\n", frequencies[i],
            freq_gains[0][i], freq_gains[1][i], freq_gains[2][i], freq_gains[3][i], freq_gains[4][i], freq_gains[5][i], freq_gains[6][i]);
    }
}
#endif // DEBUG_EXPORT_CURVES
