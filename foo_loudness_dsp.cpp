#include "../SDK/foobar2000.h"
#include "../helpers/helpers.h"
#include "resource.h"
#include "lc_filter.h"

DECLARE_COMPONENT_VERSION(
    "Loudness Compensation DSP",
    "0.1.0",
    "Loudness Compensation DSP for foobar2000\n"
    "Applies equalization based on listening volume and set parameters to compensate for psychoacoustic effects "
    "of human hearing at lower volumes.\n"
    "By Dolkar in 2026\n"
);

// We will use a helper struct to held the configuration for our
// DSP which can be used to pass around the relevant data to
// the configuration dialog. The struct also implements the 
// serialisation from and to dsp_preset instances which store
// configuration data across sessions.
struct t_loudness_compensation_config
{
    // The strength of the effect in percent.
    t_int32 m_percent;

    // The constructor sets the default values.
    t_loudness_compensation_config(t_int32 p_percent = 100) : m_percent(p_percent) {
    }

    // The GUID that identifies this DSP and its configuration.
    static const GUID& g_get_guid() {
        static const GUID guid = { 0x66fbf000, 0x9066, 0x4dbe, { 0xa7, 0x9a, 0x6f, 0x41, 0xb2, 0x68, 0x26, 0x3f } };

        return guid;
    }

    // Read data from a preset.
    bool set_data(const dsp_preset& p_data) {
        if (p_data.get_owner() != g_get_guid()) return false;
        if (p_data.get_data_size() != sizeof(t_int32)) return false;
        t_int32 temp = *(t_int32*)p_data.get_data();
        byte_order::order_le_to_native_t(temp);
        m_percent = temp;
        return true;
    }

    // Write data to a preset.
    void get_data(dsp_preset& p_data) {
        p_data.set_owner(g_get_guid());
        t_int32 temp = m_percent;
        byte_order::order_native_to_le_t(temp);
        p_data.set_data(&temp, sizeof(temp));
    }
};

// Our configuration dialog is implemented as a modal dialog.
// The helper class implements some safety measures to avoid
// opening multiple non-nested modal dialogs.
class loudness_compensation_dialog : public dialog_helper::dialog_modal
{
public:
    loudness_compensation_dialog(const dsp_preset& p_data, dsp_preset_edit_callback& p_callback) : m_old_data(p_data), m_callback(p_callback), m_dirty(false) {
        m_params.set_data(m_old_data);
    }

    virtual BOOL on_message(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_INITDIALOG:
            {
                HWND slider = GetDlgItem(get_wnd(), IDC_SLIDER);
                // Needs redraw flag set for both calls, or things will be messed
                // up if m_percent is zero.
                SendMessage(slider, TBM_SETRANGE, TRUE, MAKELONG(-50, 200));
                SendMessage(slider, TBM_SETPOS, TRUE, m_params.m_percent);
                SendMessage(slider, TBM_SETTICFREQ, 25, 0);
                update_display();
            }
            break;

            // Slider has been moved.
            case WM_HSCROLL:
            {
                m_dirty = true;
                update_display();
            }
            break;

            case WM_COMMAND:
                switch (wp) {
                    case IDOK:
                    {
                        end_dialog(1);
                    }
                    break;

                    case IDCANCEL:
                    {
                        m_callback.on_preset_changed(m_old_data);
                        end_dialog(0);
                    }
                    break;
                }
                break;
        }
        return 0;
    }

private:
    void update_display() {
        m_params.m_percent = SendDlgItemMessage(get_wnd(), IDC_SLIDER, TBM_GETPOS, 0, 0);
        if (m_dirty) {
            dsp_preset_impl data;
            m_params.get_data(data);
            m_callback.on_preset_changed(data);
            m_dirty = false;
        }

        uSetDlgItemText(get_wnd(), IDC_STATIC_DISPLAY,
            pfc::string_formatter() << pfc::format_int(m_params.m_percent) << "%");
    }

    bool m_dirty;
    const dsp_preset& m_old_data;
    t_loudness_compensation_config m_params;
    dsp_preset_edit_callback& m_callback;
};

// Monitors the current volume so we can retrieve it from the dsp thread
class volume_monitor : public play_callback_static
{
public:
    // Initialization
    void on_playback_starting(play_control::t_track_command p_command, bool p_paused) override {
        static_api_ptr_t<playback_control> pc;
        m_volume = pc->get_volume();
    }

    // Received callback
    void on_volume_change(float p_new_val) override
    {
        pfc::mutexScope guard(m_guard);
        m_volume = p_new_val;
    }

    // Retrieve the current volume in dB
    float get_volume() {
        pfc::mutexScope guard(m_guard);
        return m_volume;
    }
    // We only care about volume change
    unsigned get_flags() override
    {
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

// Finally, this is our DSP class. We need a few extra methods
// to support presets.
class loudness_compensation_dsp : public dsp_impl_base
{
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
        if (params.set_data(p_data)) {
            loudness_compensation_dialog dlg(p_data, p_callback);
            // TODO dialog_helper::dialog_modal is deprecated
            dlg.run(IDD_CONFIG, p_parent);
        }
    }

    // Return our default preset.
    static bool g_get_default_preset(dsp_preset& p_out) {
        t_loudness_compensation_config().get_data(p_out);
        return true;
    }

    // Read parameters from the provided preset.
    bool set_data(const dsp_preset& p_data) {
        t_loudness_compensation_config params;
        if (!params.set_data(p_data)) return false;
        //m_factor = (audio_sample)(params.m_percent * 0.01);
        return true;
    }

    virtual void on_endoftrack(abort_callback& p_abort) {
        flush();
    }

    virtual void on_endofplayback(abort_callback& p_abort) {
        flush();
    }

    // Process chunk.
    virtual bool on_chunk(audio_chunk* chunk, abort_callback& p_abort) {
        unsigned channel_count = chunk->get_channels();
        t_size sample_count = chunk->get_sample_count();
        unsigned sample_rate = chunk->get_sample_rate();
        float current_loudness = get_current_loudness();

        // Initialize
        if (!active_filter) {
            prepare_active_filter(channel_count, sample_rate, current_loudness);
        }

        // Test for changed parameters
        const lc_filter::config& curr_cfg = active_filter->get_config();
        if (curr_cfg.channel_count != channel_count || curr_cfg.sample_rate != sample_rate || curr_cfg.current_loudness != current_loudness) {
            prepare_active_filter(channel_count, sample_rate, current_loudness);
        }

        FB2K_DebugLog() << "Sample count: " << sample_count << " Current loudness: " << pfc::format_float(current_loudness, 0, 1) << " phon";

        // Process signal
        active_filter->process(chunk->get_data(), sample_count);

        // Add (modified) input chunk to output.
        return true;
    }

    virtual void flush() {
        if (!active_filter) {
            active_filter->flush();
        }
        // Nothing to flush.
    }

    virtual double get_latency() {
        // We have no buffer, so latency is 0.
        return 0.0;
    }

    virtual bool need_track_change_mark() {
        return false;
    }

private:
    void prepare_active_filter(int channel_count, int sample_rate, float loudness) {
        lc_filter::config cfg;
        cfg.sample_rate = sample_rate;
        cfg.channel_count = channel_count;
        // Reference SPL of a -20 dBFS RMS pink noise: ~83 dB
        // This 83 dB level corresponds to a perceived loudness of 80 phon
        // Typical mastering target: -9 LUFS
        cfg.reference_loudness = 80.0f + (20.0f - 9.0f);
        cfg.current_loudness = loudness;

        // We have room allowed by volume control
        float volume = foo_loudness_dsp_volume_monitor.get_static_instance().get_volume();
        cfg.clipping_threshold = -volume;
        cfg.debug = true;

        if (!active_filter) {
            active_filter = std::make_unique<lc_filter>(cfg);
        } else {
            active_filter->reset(cfg);
        }
    }

    float get_current_loudness() const {
        float volume = foo_loudness_dsp_volume_monitor.get_static_instance().get_volume();

        float max_volume_loudness = 90.0f;
        float loudness = max_volume_loudness + volume;
        // clamp to valid range
        return std::clamp(loudness, 20.0f, 100.0f);
    }

    std::unique_ptr<lc_filter> active_filter;
    std::unique_ptr<lc_filter> fadeout_filter;
};

static dsp_factory_t<loudness_compensation_dsp> foo_loudness_dsp;
