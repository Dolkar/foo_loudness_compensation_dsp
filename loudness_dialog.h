#pragma once

#include "../SDK/foobar2000.h"
#include "../helpers/helpers.h"
#include "resource.h"

// Holds plugin configuration
struct t_loudness_compensation_config
{
    // Reference monitoring SPL of a -20 dBFS RMS pink noise: 78 dB
    // Typical mastering target: -14 LUFS
    // https://www.production-expert.com/production-expert-1/understanding-loudness-part-3-calibrating-your-monitors
    static constexpr t_int32 s_default_reference_spl = 78 + (20 - 14);

    // The GUID that identifies this DSP and its configuration.
    static const GUID& g_get_guid() {
        static const GUID guid = { 0x66fbf000, 0x9066, 0x4dbe, { 0xa7, 0x9a, 0x6f, 0x41, 0xb2, 0x68, 0x26, 0x3f } };
        return guid;
    }

    // The constructor sets the default values.
    t_loudness_compensation_config(t_int32 full_volume_spl = 90, t_int32 reference_spl = s_default_reference_spl, t_int32 passthrough = 0) :
        m_full_volume_spl(full_volume_spl), m_reference_spl(reference_spl) {
    }

    // Read data from a preset.
    bool set_data(const dsp_preset& p_data);
    // Write data to a preset.
    void get_data(dsp_preset& p_data);

    // The SPL level at full volume in dB
    t_int32 m_full_volume_spl;
    // The reference SPL level in dB
    t_int32 m_reference_spl;
};

// Message ID to send current volume updates
#define WM_UPDATE_VOLUME (WM_USER + 123)

// Our configuration dialog is implemented as a modal dialog.
// The helper class implements some safety measures to avoid
// opening multiple non-nested modal dialogs.
class loudness_compensation_dialog : public dialog_helper::dialog_modal
{
public:
    // Active dialog window
    static HWND s_active_dialog;
    // Whether passthrough is checked in the dialog
    static bool s_passthrough;

    loudness_compensation_dialog(const dsp_preset& p_data, dsp_preset_edit_callback& p_callback, int current_volume) :
        m_old_data(p_data), m_callback(p_callback), m_current_volume(current_volume) {
        m_params.set_data(m_old_data);
    }

    // Event handler
    virtual BOOL on_message(UINT msg, WPARAM wp, LPARAM lp);

private:
    // Updates all displayed values (overwrites user values)
    void update_display();
    // Updates volume-based values (not editable by user)
    void update_volume_display();
    // Registers anything the user may have changed, returns true if it did
    bool register_changes(bool force_register = false);

    // Old preset data used when the dialog window is cancelled
    const dsp_preset& m_old_data;
    // Current volume in foobar (hopefully up to date)
    int m_current_volume;
    // Current preset data
    t_loudness_compensation_config m_params;
    // Callback on preset change
    dsp_preset_edit_callback& m_callback;
};
