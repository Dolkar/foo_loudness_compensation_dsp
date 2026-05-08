#include <afxwin.h>
#include "../SDK/foobar2000.h"
#include "loudness_dialog.h"
#include "lc_filter.h"
#define _USE_MATH_DEFINES
#include <math.h>

DECLARE_COMPONENT_VERSION(
    "Loudness Compensation DSP",
    "0.1.0",
    "Loudness Compensation DSP for foobar2000\n"
    "Applies equalization based on listening volume and set parameters to compensate for psychoacoustic effects "
    "of human hearing at lower volumes.\n"
    "By Dolkar in 2026\n"
);

// Monitors the current volume so we can retrieve it from the dsp thread or signal to UI
class volume_monitor : public play_callback_static
{
public:
    // Initialization
    void on_playback_starting(play_control::t_track_command p_command, bool p_paused) override {
        static_api_ptr_t<playback_control> pc;
        m_volume = pc->get_volume();
    }

    // Received callback
    void on_volume_change(float p_new_val) override {
        {
            pfc::mutexScope guard(m_guard);
            m_volume = p_new_val;
        }

        // Notify the options dialog if open
        if (loudness_compensation_dialog::s_active_dialog) {
            PostMessage(loudness_compensation_dialog::s_active_dialog, WM_UPDATE_VOLUME, get_volume_int(), 0);
        }
    }

    // Retrieve the current volume in dB
    float get_volume() {
        pfc::mutexScope guard(m_guard);
        return m_volume;
    }

    // Retrieve the current volume in dB, rounded
    int get_volume_int() {
        float volume = get_volume();
        return static_cast<int>(std::roundf(volume));
    }

    // We only care about volume change
    unsigned get_flags() override {
        return play_callback::flag_on_playback_starting | play_callback::flag_on_volume_change;
    }
    void on_playback_new_track(metadb_handle_ptr p_track) override {}
    void on_playback_stop(play_control::t_stop_reason p_reason) override {}
    void on_playback_seek(double p_time) override {}
    void on_playback_pause(bool p_state) override {}
    void on_playback_edited(metadb_handle_ptr p_track) override {}
    void on_playback_dynamic_info(const file_info& p_info) override {}
    void on_playback_dynamic_info_track(const file_info& p_info) override {}
    void on_playback_time(double p_time) override {}

private:
    float m_volume = -20.0f;
    pfc::mutex m_guard;
};

static play_callback_static_factory_t<volume_monitor> foo_loudness_dsp_volume_monitor;

void crossfade(float t, float& gain_out, float& gain_in) {
    t = std::clamp(t, 0.0f, 1.0f);
    gain_in = sin(M_PI / 2.0 * t);
    gain_out = cos(M_PI / 2.0 * t);
}

// The DSP class adapter for foobar. Manages filters and implements crossfade
class loudness_compensation_dsp : public dsp_impl_base
{
    static const int s_crossfade_samples = 500;
public:
    loudness_compensation_dsp(const dsp_preset& p_data) {
        set_data(p_data);
    }

    static GUID g_get_guid() {
        return t_loudness_compensation_config::g_get_guid();
    }

    static void g_get_name(pfc::string_base& p_out) {
        p_out = "Loudness Compensation DSP";
    }

    // Return if we have a configuration popup (we do).
    static bool g_have_config_popup() {
        return true;
    }

    // Show our configuration popup.
    // The provided callback is used to report configuration changes back to the caller.
    static void g_show_config_popup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback) {
        t_loudness_compensation_config params;
        params.set_data(p_data);

        loudness_compensation_dialog dlg(p_data, p_callback, foo_loudness_dsp_volume_monitor.get_static_instance().get_volume_int());
        // TODO dialog_helper::dialog_modal is deprecated
        dlg.run(IDD_CONFIG, p_parent);
    }

    // Return our default preset.
    static bool g_get_default_preset(dsp_preset& p_out) {
        t_loudness_compensation_config().get_data(p_out);
        return true;
    }

    // Read parameters from the provided preset.
    bool set_data(const dsp_preset& p_data) {
        return m_params.set_data(p_data);
    }

    virtual void on_endoftrack(abort_callback& p_abort) {
        flush();
    }

    virtual void on_endofplayback(abort_callback& p_abort) {
        flush();
    }

    // Process chunk.
    virtual bool on_chunk(audio_chunk* chunk, abort_callback& p_abort) {
        // Fetch parameters
        unsigned channel_count = chunk->get_channels();
        t_size sample_count = chunk->get_sample_count();
        unsigned sample_rate = chunk->get_sample_rate();
        float current_spl = get_current_spl();
        float reference_spl = m_params.m_reference_spl;
        float strength = loudness_compensation_dialog::s_passthrough ? 0.0f : 1.0f;

        // Initialize
        if (!m_active_filter) {
            prepare_active_filter(channel_count, sample_rate, current_spl, reference_spl, strength);
        }

        // Test for changed filter parameters
        const lc_filter::config& curr_cfg = m_active_filter->get_config();

        // We can't fade if these have changed:
        if (curr_cfg.channel_count != channel_count || curr_cfg.sample_rate != sample_rate) {
            flush();
            prepare_active_filter(channel_count, sample_rate, current_spl, reference_spl, strength);
        } // Fade when volume or other parameters have changed, but not when already fading:
        else if (m_fadeout_lifetime <= 0 && (
            current_spl != curr_cfg.current_spl ||
            reference_spl != curr_cfg.reference_spl ||
            strength != curr_cfg.strength)) {

            std::swap(m_active_filter, m_fadeout_filter);
            prepare_active_filter(channel_count, sample_rate, current_spl, reference_spl, strength);

            // Leave half of ir_length for the new filter to warm up
            m_fadeout_lifetime = curr_cfg.ir_length / 2 + s_crossfade_samples;
        }

        if (m_fadeout_lifetime > 0) {
            // Process chunk with old filter
            m_fade_buffer.copy(*chunk);
            m_fadeout_filter->process(m_fade_buffer.get_data(), sample_count);
        }

        // Process chunk with active filter
        m_active_filter->process(chunk->get_data(), sample_count);

        // Apply crossfade
        if (m_fadeout_lifetime > 0) {
            audio_sample* out_data = chunk->get_data();
            audio_sample* fade_data = m_fade_buffer.get_data();

            for (unsigned i = 0; i < sample_count; i++) {
                float t = m_fadeout_lifetime / (float)s_crossfade_samples;
                float old_f, new_f;
                crossfade(t, new_f, old_f);

                for (unsigned channel = 0; channel < channel_count; channel++) {
                    unsigned si = i * channel_count + channel;
                    out_data[si] = old_f * fade_data[si] + new_f * out_data[si];
                }
                m_fadeout_lifetime--;
            }
        }

        // Add (modified) input chunk to output.
        return true;
    }

    virtual void flush() {
        if (m_active_filter) {
            m_active_filter->flush();
        }
        if (m_fadeout_filter) {
            m_fadeout_filter->flush();
        }
        m_fade_buffer.reset();
        m_fadeout_lifetime = 0;
    }

    virtual double get_latency() {
        // We use a zero latency filter
        return 0.0;
    }

    virtual bool need_track_change_mark() {
        return false;
    }

private:
    void prepare_active_filter(int channel_count, int sample_rate, float current_spl, float reference_spl, float strength) {
        lc_filter::config cfg;
        cfg.sample_rate = sample_rate;
        cfg.channel_count = channel_count;
        cfg.reference_spl = reference_spl;
        cfg.current_spl = current_spl;
        cfg.strength = strength;
        // We have clipping room allowed by volume control
        float volume = foo_loudness_dsp_volume_monitor.get_static_instance().get_volume();
        cfg.clipping_threshold = -volume;

#ifdef NDEBUG
        cfg.debug = false;
#else
        cfg.debug = true;
#endif // NDEBUG

        if (!m_active_filter) {
            m_active_filter = std::make_unique<lc_filter>(cfg);
        } else {
            m_active_filter->reset(cfg);
        }
    }

    float get_current_spl() const {
        float volume = foo_loudness_dsp_volume_monitor.get_static_instance().get_volume();
        float spl = m_params.m_full_volume_spl + volume;
        // clamp to valid range
        return std::clamp(spl, 20.0f, 100.0f);
    }

    // Current config parameters
    t_loudness_compensation_config m_params;
    // The currently active filter (if any)
    std::unique_ptr<lc_filter> m_active_filter;
    // The filter currently being faded out
    std::unique_ptr<lc_filter> m_fadeout_filter;
    // Samples until fadeout filter can be turned off
    int m_fadeout_lifetime = 0;
    // Buffer used for fading
    audio_chunk_impl m_fade_buffer;
};

static dsp_factory_t<loudness_compensation_dsp> foo_loudness_dsp;
