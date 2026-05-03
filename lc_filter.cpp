#include "lc_filter.h"

#include "freq_to_ir.h"
#include "compensation_curve.h"
#include "../SDK/foobar2000.h"

void lc_filter::reset(const config& cfg)
{
    m_cfg = cfg;
    for (auto& conv : m_convolvers)
        conv->reset();

    if (m_cfg.debug) {
        FB2K_DebugLog() << "Reset!";
    }

    prepare();
}

void lc_filter::flush()
{
    for (auto& conv : m_convolvers)
        conv->flush();

    if (m_cfg.debug) {
        FB2K_DebugLog() << "Flush!";
    }
}

void lc_filter::process(audio_sample* sample_data, t_size sample_count)
{
    if (sample_count > m_input_buffer.size()) {
        m_input_buffer.resize(sample_count);
        m_output_buffer.resize(sample_count);
    }

    t_size stride = m_cfg.channel_count;
    for (int channel = 0; channel < m_cfg.channel_count; channel++) {
        // Copy to input buffer
        t_size offset = channel;
        for (t_size i = 0; i < sample_count; i++) {
            m_input_buffer[i] = sample_data[channel + i * stride];
        }

        m_convolvers[channel]->process(m_input_buffer.data(), m_output_buffer.data(), sample_count);

        // Copy it back, overwriting source signal
        for (t_size i = 0; i < sample_count; i++) {
            sample_data[channel + i * stride] = m_output_buffer[i];
        }
    }
}

void lc_filter::prepare()
{
    if (m_cfg.debug) {
        FB2K_DebugLog() << "Preparing filter with parameters:";
        FB2K_DebugLog() << "Sample rate: " << m_cfg.sample_rate;
        FB2K_DebugLog() << "Channel count: " << m_cfg.channel_count;
        FB2K_DebugLog() << "Reference loudness: " << pfc::format_float(m_cfg.reference_loudness, 0, 1) << " phon";
        FB2K_DebugLog() << "Current loudness: " << pfc::format_float(m_cfg.current_loudness, 0, 1) << " phon";
    }

    // set up frequency range to sample the curve on
    std::vector<float> freq_points;
    freq_points.resize(m_cfg.spectrum_resolution);

    for (int i = 0; i < m_cfg.spectrum_resolution; i++) {
        // use log distribution
        float log_freq = (logf(m_cfg.max_frequency) - logf(m_cfg.min_frequency)) * i / (m_cfg.spectrum_resolution - 1.0f) + logf(m_cfg.min_frequency);
        freq_points[i] = expf(log_freq);
    }

    // sample the curve
    std::vector<float> db_deltas;
    db_deltas.resize(freq_points.size());
    make_compensation_curve(m_cfg.reference_loudness, m_cfg.current_loudness, m_cfg.clipping_threshold, freq_points.data(), freq_points.size(), db_deltas.data());

    if (m_cfg.debug) {
        float mindb = db_deltas[0];
        float maxdb = db_deltas[0];
        for (int i = 1; i < db_deltas.size(); i++) {
            if (db_deltas[i] < mindb)
                mindb = db_deltas[i];
            if (db_deltas[i] > maxdb)
                maxdb = db_deltas[i];
        }

        FB2K_DebugLog() << "Compensation curve range: " << pfc::format_float(mindb, 0, 2) << " dB : " << pfc::format_float(maxdb, 0, 2) << " dB.";
    }

    // form the IR
    std::vector<float> ir;
    ir.resize(m_cfg.ir_length);
    freq_to_ir(m_cfg.sample_rate, freq_points.data(), db_deltas.data(), db_deltas.size(), 8.0f, ir.data(), ir.size(), m_cfg.debug);

    // initialize the convolvers
    m_convolvers.resize(m_cfg.channel_count);
    for (int i = 0; i < m_cfg.channel_count; i++) {
        if (!m_convolvers[i]) {
            m_convolvers[i] = std::make_unique<fftconvolver::FFTConvolver>();
        }
        m_convolvers[i]->init(m_cfg.convolution_block_size, ir.data(), ir.size());
    }
}
