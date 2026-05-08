#include "loudness_dialog.h"

#include "compensation_curve.h"
#include <algorithm>

bool t_loudness_compensation_config::set_data(const dsp_preset& p_data) {
    if (p_data.get_owner() != g_get_guid()) return false;
    if (p_data.get_data_size() != sizeof(t_loudness_compensation_config)) return false;

    const t_int32* ptr = (const t_int32*)p_data.get_data();
    m_full_volume_spl = *(ptr++);
    byte_order::order_le_to_native_t(m_full_volume_spl);
    m_reference_spl = *(ptr++);
    byte_order::order_le_to_native_t(m_reference_spl);
    return true;
}

void t_loudness_compensation_config::get_data(dsp_preset& p_data) {
    p_data.set_owner(g_get_guid());
    t_int32 temp[2] = { m_full_volume_spl, m_reference_spl };
    byte_order::order_native_to_le_t(temp[0]);
    byte_order::order_native_to_le_t(temp[1]);
    p_data.set_data(&temp, sizeof(temp));
}


HWND loudness_compensation_dialog::s_active_dialog = NULL;
bool loudness_compensation_dialog::s_passthrough = false;

BOOL loudness_compensation_dialog::on_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
        {
            s_active_dialog = get_wnd();

            // Setup gain chart
            m_chart.subclass_dlg_item(IDC_CHART, CWnd::FromHandle(get_wnd()));
            m_chart.set_active(true);
            update_display();
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_FULL_VOLUME_SPL:
                case IDC_REFERENCE_SPL:
                {
                    WORD notifyCode = HIWORD(wp);
                    if (notifyCode == EN_KILLFOCUS) {
                        register_changes();
                    }
                    break;
                }
                case IDC_PASSTHROUGH:
                {
                    s_passthrough = SendDlgItemMessage(get_wnd(), IDC_PASSTHROUGH, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    update_volume_display();
                    break;
                }
                case IDOK:
                {
                    // Only exit if there were no new changes
                    if (!register_changes())
                        end_dialog(1);
                    break;
                }
                case IDCANCEL:
                {
                    m_callback.on_preset_changed(m_old_data);
                    end_dialog(0);
                    break;
                }
                case IDC_RESET:
                {
                    m_params = {};
                    s_passthrough = false;
                    update_display();
                    register_changes(true);
                    break;
                }
            }
            break;
        case WM_UPDATE_VOLUME:
        {
            m_current_volume = static_cast<int>(wp);
            update_volume_display();
            break;
        }
        case WM_DESTROY:
        {
            s_active_dialog = NULL;
            // Never preserve the passthrough toggle, it's just for the user to easily test the DSP plugin
            s_passthrough = false;
            break;
        }
    }
    return 0;
}

// Helper function for reading a validated value out of a control
inline bool read_validate_spl(HWND hwnd, int dlg, int min_value, int max_value, int& new_value) {
    BOOL translated = TRUE;
    int value = GetDlgItemInt(hwnd, dlg, &translated, TRUE);
    if (translated) {
        value = std::clamp(value, min_value, max_value);
        SetDlgItemInt(hwnd, dlg, value, TRUE);

        if (value != new_value) {
            new_value = value;
            return true;
        }
    }
    return false;
}

void loudness_compensation_dialog::update_display() {
    SetDlgItemInt(get_wnd(), IDC_FULL_VOLUME_SPL, m_params.m_full_volume_spl, TRUE);
    SetDlgItemInt(get_wnd(), IDC_REFERENCE_SPL, m_params.m_reference_spl, TRUE);
    SendDlgItemMessage(get_wnd(), IDC_PASSTHROUGH, BM_SETCHECK, BST_UNCHECKED, 0);
    update_volume_display();
}

void loudness_compensation_dialog::update_volume_display() {
    SetDlgItemInt(get_wnd(), IDC_CURRENT_VOLUME, m_current_volume, TRUE);
    SetDlgItemInt(get_wnd(), IDC_CURRENT_VOLUME_SPL, m_params.m_full_volume_spl + m_current_volume, TRUE);

    // When volume changes, we also need to update the graph
    // Use log distribution for frequency points
    std::vector<float> freq_points;
    freq_points.resize(100);
    for (int i = 0; i < freq_points.size(); i++) {
        float log_freq = (logf(20000.0f) - logf(20.0f)) * i / (freq_points.size() - 1.0f) + logf(20.0f);
        freq_points[i] = expf(log_freq);
    }

    // Like in lc_filter, assume SPL ~= loudness
    float ref_loudness = std::clamp((float)m_params.m_reference_spl, 20.0f, 100.0f);
    float current_loudness = std::clamp((float)(m_params.m_full_volume_spl + m_current_volume), 20.0f, 100.0f);
    float clipping_threshold = (float)-m_current_volume;

    std::vector<float> gains;
    gains.resize(freq_points.size());
    make_compensation_curve(ref_loudness, current_loudness, clipping_threshold, freq_points.data(), freq_points.size(), gains.data());

    m_chart.set_data_points(freq_points, gains, clipping_threshold);
    m_chart.set_active(!s_passthrough);
}

bool loudness_compensation_dialog::register_changes(bool force_register) {
    bool changed = force_register;
    changed |= read_validate_spl(get_wnd(), IDC_FULL_VOLUME_SPL, 20, 120, m_params.m_full_volume_spl);
    changed |= read_validate_spl(get_wnd(), IDC_REFERENCE_SPL, 60, 100, m_params.m_reference_spl);

    if (changed) {
        dsp_preset_impl data;
        m_params.get_data(data);
        m_callback.on_preset_changed(data);
    }

    update_volume_display();
    return changed;
}
