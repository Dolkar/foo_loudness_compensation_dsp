#pragma once

#include <afxwin.h>
#include <vector>

// Custom UI control for displaying a gain graph
class gain_chart_ctrl : public CWnd {
    DECLARE_DYNAMIC(gain_chart_ctrl)

public:
    gain_chart_ctrl();
    virtual ~gain_chart_ctrl();

    // Attach to an existing custom control in a dialog resource
    BOOL subclass_dlg_item(UINT nID, CWnd* pParent);
    // Replace the displayed curve data (frequency in Hz, gain in dB), along with a limiting line
    void set_data_points(const std::vector<float>& points_x, const std::vector<float>& points_y, float limit_y);
    // Toggle between blue (active) and grey (inactive) curve color
    void set_active(bool bActive);

protected:
    afx_msg void OnPaint();
    DECLARE_MESSAGE_MAP()

private:
    void draw_chart(CDC& dc, CRect rect_client);
    void draw_x_axis(CDC& dc, CRect rect_plot);
    void draw_y_axis(CDC& dc, CRect rect_plot);
    void draw_curves(CDC& dc, CRect rect_plot);

    // Convert data coordinates to pixel positions within the plot area
    float x_value_to_pixel(float x_val, const CRect& rect_plot) const;
    float y_value_to_pixel(float y_val, const CRect& rect_plot) const;

    std::vector<float> m_data_points_x;
    std::vector<float> m_data_points_y;
    float m_limit_y = 0.0f;
    bool m_active = true;
};
