#pragma once

#include <FFTConvolver/FFTConvolver.h>
#include "../SDK/foobar2000.h"

class lc_filter {
public:
    struct config {
        // The audio sample rate
        int sample_rate;
        // Number of channels
        int channel_count;
        // The reference SPL value in dB for the loudness compensation target
        // Aka how loud the music is assumed to have been mastered for
        float reference_spl;
        // The current SPL value the track is being listened to in dB
        float current_spl;
        // The clipping threshold, as a max dB delta. The signal may be scaled down
        // to avoid clipping if the EQ exceeds this value.
        float clipping_threshold = 0.0f;
        // Additional scaling of the effect
        float strength = 1.0f;
        // The resolution of the compensation spectrum
        int spectrum_resolution = 128;
        // The minimum defined frequency for the spectrum
        float min_frequency = 20.0f;
        // The maximum defined frequency for the spectrum
        float max_frequency = 20000.0f;
        // The length of the impulse response. Higher values increase latency
        // and processing time, but increase quality
        int ir_length = 4096;
        // Internal block size of the convolver. Lower numbers decrease latency
        // at the cost of higher CPU usage
        int convolution_block_size = 512;
        // Debug mode toggle
        bool debug = false;
    };

    explicit lc_filter(const config& cfg) { reset(cfg); }

    void reset(const config& cfg);

    void flush();

    // Processes the given audio data, modifying it in-place
    void process(audio_sample* sample_data, t_size sample_count);

    const config& get_config() const {
        return m_cfg;
    }

private:
    void prepare();

    // The config
    config m_cfg;
    // Input buffer for one channel
    std::vector<fftconvolver::Sample> m_input_buffer;
    // Output buffer for one channel
    std::vector<fftconvolver::Sample> m_output_buffer;
    // Convolver for each channel
    std::vector<std::unique_ptr<fftconvolver::FFTConvolver>> m_convolvers;
};
