#include "gain_chart_ctrl.h"

#include <afxwin.h>
#include <gdiplus.h>
#include <cmath>
#include <vector>

// Chart axis ranges
constexpr float Y_MIN_DB = -20.0f;
constexpr float Y_MAX_DB = 20.0f;
constexpr float X_MIN_HZ = 20.0f;
constexpr float X_MAX_HZ = 20000.0f;

// Pixel layout constants for margins around the plotting area
constexpr int MARGIN_LEFT = 60;
constexpr int MARGIN_RIGHT = 20;
constexpr int MARGIN_TOP = 30;
constexpr int MARGIN_BOTTOM = 30;
constexpr int TITLE_HEIGHT = 30;

// Standard audio frequency labels for the logarithmic X-axis
constexpr float TICK_FREQS_HZ[] = {
    20.0f, 40.0f, 100.0f,
    200.0f, 400.0f, 1000.0f,
    2000.0f, 4000.0f, 10000.0f,
    20000.0f
};
constexpr int NUM_FREQ_TICKS = sizeof(TICK_FREQS_HZ) / sizeof(float);

// GDI+ init and shutdown
class CGdiPlusWrapper {
public:
    static CGdiPlusWrapper& Instance() {
        static CGdiPlusWrapper inst;
        return inst;
    }

    ULONG_PTR GetToken() const { return m_gdiplusToken; }

private:
    CGdiPlusWrapper() {
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        gdiplusStartupInput.SuppressBackgroundThread = TRUE; // Can be useful for a DLL
        Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);
    }

    ~CGdiPlusWrapper() {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
    }

    ULONG_PTR m_gdiplusToken;

    // Prevent copy/assign
    CGdiPlusWrapper(const CGdiPlusWrapper&) = delete;
    CGdiPlusWrapper& operator=(const CGdiPlusWrapper&) = delete;
};

IMPLEMENT_DYNAMIC(gain_chart_ctrl, CWnd)

BEGIN_MESSAGE_MAP(gain_chart_ctrl, CWnd)
    ON_WM_PAINT()
END_MESSAGE_MAP()

gain_chart_ctrl::gain_chart_ctrl() {
    // Initialize GDI+
    CGdiPlusWrapper::Instance();

    // Register the window class once per process
    static bool s_class_registered = false;
    if (!s_class_registered) {
        WNDCLASS wndcls;
        memset(&wndcls, 0, sizeof(wndcls));
        wndcls.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wndcls.lpfnWndProc = ::DefWindowProc;
        wndcls.hInstance = AfxGetInstanceHandle();
        wndcls.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
        wndcls.hbrBackground = (HBRUSH)::GetStockObject(WHITE_BRUSH);
        wndcls.lpszClassName = _T("gain_chart_ctrl");
        if (!AfxRegisterClass(&wndcls)) {
            AfxThrowResourceException();
        }
        s_class_registered = true;
    }
}

gain_chart_ctrl::~gain_chart_ctrl() = default;

BOOL gain_chart_ctrl::subclass_dlg_item(UINT nID, CWnd* pParent) {
    ASSERT(pParent != nullptr);
    HWND hWnd = ::GetDlgItem(pParent->GetSafeHwnd(), nID);
    if (hWnd == nullptr) {
        return FALSE;
    }
    return SubclassWindow(hWnd);
}

void gain_chart_ctrl::set_data_points(const std::vector<float>& points_x, const std::vector<float>& points_y, float limit_y) {
    m_data_points_x = points_x;
    m_data_points_y = points_y;
    m_limit_y = limit_y;
    ASSERT(points_x.size() == points_y.size());
    if (GetSafeHwnd() != nullptr) {
        Invalidate();
    }
}

void gain_chart_ctrl::set_active(bool bActive) {
    m_active = bActive;
    if (GetSafeHwnd() != nullptr) {
        Invalidate();
    }
}

void gain_chart_ctrl::OnPaint() {
    CPaintDC dc_paint(this);
    CRect rect_client;
    GetClientRect(rect_client);
    if (rect_client.Width() <= 0 || rect_client.Height() <= 0) {
        return;
    }

    // Double-buffer to eliminate flicker
    CDC mem_dc;
    mem_dc.CreateCompatibleDC(&dc_paint);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(&dc_paint, rect_client.Width(), rect_client.Height());
    CBitmap* old_bmp = mem_dc.SelectObject(&bmp);

    mem_dc.FillSolidRect(rect_client, RGB(255, 255, 255));
    draw_chart(mem_dc, rect_client);

    dc_paint.BitBlt(0, 0, rect_client.Width(), rect_client.Height(), &mem_dc, 0, 0, SRCCOPY);
    mem_dc.SelectObject(old_bmp);
    mem_dc.DeleteDC();
    bmp.DeleteObject();
}

void gain_chart_ctrl::draw_chart(CDC& dc, CRect rect_client) {
    // Calculate the actual plotting area, leaving room for labels and title
    CRect rect_plot(
        rect_client.left + MARGIN_LEFT,
        rect_client.top + MARGIN_TOP,
        rect_client.right - MARGIN_RIGHT,
        rect_client.bottom - MARGIN_BOTTOM);

    // Draw title at the top
    CFont font;
    font.CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, _T("Tahoma"));
    CFont* old_font = dc.SelectObject(&font);
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(0, 0, 0));
    dc.DrawText(_T("Compensation EQ Gain"), CRect(rect_client.left, rect_client.top,
        rect_client.right, rect_client.top + TITLE_HEIGHT),
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    dc.SelectObject(old_font);

    draw_x_axis(dc, rect_plot);
    draw_y_axis(dc, rect_plot);
    draw_curves(dc, rect_plot);
}

void gain_chart_ctrl::draw_y_axis(CDC& dc, CRect rect_plot) {
    CPen pen_major(PS_SOLID, 1, RGB(0, 0, 0)); // labelled tick marks
    CPen pen_minor(PS_SOLID, 1, RGB(192, 192, 192)); // unlabelled tick marks

    // Ensure the margin area left of the plot is clean
    CRect left_area(rect_plot.left - MARGIN_LEFT, rect_plot.top,
        rect_plot.left, rect_plot.bottom);
    dc.FillSolidRect(left_area, RGB(255, 255, 255));

    CFont font;
    font.CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, _T("Tahoma"));
    CFont* old_font = dc.SelectObject(&font);

    // Draw ticks from top (+20 dB) to bottom (-20 dB) in 5 dB steps
    for (float db = Y_MAX_DB; db >= Y_MIN_DB; db -= 5.0f) {
        int y = static_cast<int>(y_value_to_pixel(db, rect_plot));
        bool b_label = (static_cast<int>(db) % 10 == 0); // label every 10 dB

        CPen& pen = (b_label ? pen_major : pen_minor);
        CPen* old_pen = dc.SelectObject(&pen);

        // Small tick mark inside the margin
        dc.MoveTo(rect_plot.left - 5, y);
        dc.LineTo(rect_plot.left, y);

        // Faint horizontal grid line at labelled intervals
        // A thick zero line spans the full plot width and we draw it elsewhere
        if (static_cast<int>(db) != 0) {
            CPen grid_pen(PS_SOLID, 1, RGB(220, 220, 220));
            dc.SelectObject(&grid_pen);
            dc.MoveTo(rect_plot.left, y);
            dc.LineTo(rect_plot.right, y);
        }
        dc.SelectObject(old_pen);

        // Draw the dB label to the left of the plot
        if (b_label) {
            CString str;
            str.Format(_T("%+d dB"), static_cast<int>(db));
            CRect r_text(rect_plot.left - MARGIN_LEFT, y - 10,
                rect_plot.left - 10, y + 10);
            dc.DrawText(str, r_text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    dc.SelectObject(old_font);
}

void gain_chart_ctrl::draw_x_axis(CDC& dc, CRect rect_plot) {
    // Clean the bottom margin area for labels
    CRect bottom_area(rect_plot.left, rect_plot.bottom,
        rect_plot.right, rect_plot.bottom + MARGIN_BOTTOM);
    dc.FillSolidRect(bottom_area, RGB(255, 255, 255));

    CFont font;
    font.CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, _T("Tahoma"));
    CFont* old_font = dc.SelectObject(&font);
    dc.SetBkMode(TRANSPARENT);

    CPen pen_tick(PS_SOLID, 1, RGB(0, 0, 0));
    CPen* old_pen = dc.SelectObject(&pen_tick);

    for (int i = 0; i < NUM_FREQ_TICKS; ++i) {
        float freq = TICK_FREQS_HZ[i];
        if (freq < X_MIN_HZ || freq > X_MAX_HZ) {
            continue;
        }

        int x = static_cast<int>(x_value_to_pixel(freq, rect_plot));

        // Small tick mark below the plot
        dc.MoveTo(x, rect_plot.bottom);
        dc.LineTo(x, rect_plot.bottom + 4);

        // Format label: plain number below 1 kHz, then "1k", "2k", etc.
        CString str;
        if (freq < 1000.0f) {
            str.Format(_T("%.0f"), freq);
        } else {
            str.Format(_T("%.0fk"), freq / 1000.0f);
        }

        CRect r_text(x - 20, rect_plot.bottom + 6, x + 20, rect_plot.bottom + MARGIN_BOTTOM);
        dc.DrawText(str, r_text, DT_CENTER | DT_TOP | DT_SINGLELINE);

        // Faint vertical grid lines
        CPen grid_pen(PS_SOLID, 1, RGB(220, 220, 220));
        dc.SelectObject(&grid_pen);
        dc.MoveTo(x, rect_plot.top);
        dc.LineTo(x, rect_plot.bottom);
        dc.SelectObject(&pen_tick);
    }

    dc.SelectObject(old_font);
    dc.SelectObject(old_pen);
}

void gain_chart_ctrl::draw_curves(CDC& dc, CRect rect_plot) {
    if (m_data_points_x.empty())
        return;

    // Create a GDI+ Graphics object from the CDC handle
    Gdiplus::Graphics graphics(dc.GetSafeHdc());
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::Color zero_color = Gdiplus::Color(0, 0, 0);
    Gdiplus::Pen zero_pen(zero_color, 2.0f);

    Gdiplus::Color limit_color = Gdiplus::Color(220, 80, 80);
    Gdiplus::Pen limit_pen(limit_color, 1.0f);

    Gdiplus::Color data_color = m_active ? Gdiplus::Color(0, 102, 204) : Gdiplus::Color(160, 160, 160);
    Gdiplus::Pen data_pen(data_color, 2.0f);

    // Draw a thick line at dB = 0
    Gdiplus::PointF zero_pts[2] = {
        Gdiplus::PointF(x_value_to_pixel(X_MIN_HZ, rect_plot), y_value_to_pixel(0.0f, rect_plot)),
        Gdiplus::PointF(x_value_to_pixel(X_MAX_HZ, rect_plot), y_value_to_pixel(0.0f, rect_plot))
    };
    graphics.DrawLines(&zero_pen, zero_pts, 2);

    // Convert data points to pixel coordinates
    std::vector<Gdiplus::PointF> pixel_pts;
    for (int i = 0; i < m_data_points_x.size(); i++) {
        float freq = m_data_points_x[i];
        float db = m_data_points_y[i];

        float x = x_value_to_pixel(freq, rect_plot);
        float y = y_value_to_pixel(db, rect_plot);
        pixel_pts.push_back(Gdiplus::PointF(x, y));
    }

    // Draw the data curve
    if (pixel_pts.size() > 1u) {
        graphics.DrawLines(&data_pen, pixel_pts.data(), static_cast<INT>(pixel_pts.size()));
    }

    // Draw a faint db limit line
    if (m_limit_y <= Y_MAX_DB) {
        // Move the line slightly up so that it always appears correctly positioned above the data curve
        Gdiplus::PointF limit_pts[2] = {
            Gdiplus::PointF(x_value_to_pixel(X_MIN_HZ, rect_plot), std::roundf(y_value_to_pixel(m_limit_y, rect_plot)) - 1.0f),
            Gdiplus::PointF(x_value_to_pixel(X_MAX_HZ, rect_plot), std::roundf(y_value_to_pixel(m_limit_y, rect_plot)) - 1.0f)
        };
        graphics.DrawLines(&limit_pen, limit_pts, 2);
    }
}

float gain_chart_ctrl::x_value_to_pixel(float x_val, const CRect& rect_plot) const {
    // Logarithmic mapping: 20 Hz -> left, 20 kHz -> right
    float log_min = log10f(X_MIN_HZ);
    float log_max = log10f(X_MAX_HZ);
    float log_val = log10f(x_val);
    float t = (log_val - log_min) / (log_max - log_min);
    return rect_plot.left + t * rect_plot.Width();
}

float gain_chart_ctrl::y_value_to_pixel(float y_val, const CRect& rect_plot) const {
    // Linear mapping: +20 dB -> top, -20 dB -> bottom
    float t = (Y_MAX_DB - y_val) / (Y_MAX_DB - Y_MIN_DB);
    return rect_plot.top + t * rect_plot.Height();
}
