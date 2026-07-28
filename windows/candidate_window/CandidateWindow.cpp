//
//  CandidateWindow.cpp — Win32 + GDI+ 自绘候选窗
//
#include "CandidateWindow.h"

#include <gdiplus.h>
#include <windowsx.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")  // UpdateLayeredWindow / AlphaBlend

using namespace Gdiplus;

namespace firewin {

static const wchar_t* kClassName = L"FireCandidateWindow";

// UTF-8 -> UTF-16
static std::wstring U8ToU16(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static Color ToColor(const fire::ColorData& c) {
    auto to8 = [](double v) -> BYTE {
        if (v < 0) v = 0; if (v > 1) v = 1; return (BYTE)(v * 255.0 + 0.5);
    };
    return Color(to8(c.opacity), to8(c.red), to8(c.green), to8(c.blue));
}

CandidateWindowController::CandidateWindowController(const fire::Config& config)
    : config_(config) {}

CandidateWindowController::~CandidateWindowController() {
    Destroy();
}

bool CandidateWindowController::IsDarkMode() {
    // 读取系统浅色/深色主题（AppsUseLightTheme=0 表示深色）
    HKEY hKey;
    DWORD value = 1, size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&value, &size);
        RegCloseKey(hKey);
    }
    return value == 0;
}

bool CandidateWindowController::Create(HINSTANCE hInst) {
    hInst_ = hInst;

    GdiplusStartupInput gi;
    if (GdiplusStartup(&gdiplusToken_, &gi, nullptr) != Gdiplus::Ok) {
        OutputDebugStringW(L"[FireIME] CandidateWindow: GdiplusStartup FAILED\n");
        gdiplusToken_ = 0;
        return false;
    }

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    // RegisterClassExW 可能因类已注册（同一进程重复激活）返回 0，不算失败。
    RegisterClassExW(&wc);

    // 无焦点浮窗：TOPMOST + NOACTIVATE + TOOLWINDOW + LAYERED（用 UpdateLayeredWindow 逐像素透明）
    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        kClassName, L"", WS_POPUP,
        0, 0, 10, 10, nullptr, nullptr, hInst, this);
    if (!hwnd_) {
        OutputDebugStringW(L"[FireIME] CandidateWindow: CreateWindowExW FAILED\n");
        return false;
    }
    return true;
}

void CandidateWindowController::Destroy() {
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    if (gdiplusToken_) { GdiplusShutdown(gdiplusToken_); gdiplusToken_ = 0; }
}

LRESULT CALLBACK CandidateWindowController::WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                                    LPARAM lParam) {
    CandidateWindowController* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<CandidateWindowController*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<CandidateWindowController*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CandidateWindowController::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam,
                                                 LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            // 关键：绝不激活本窗口，避免抢走宿主输入框焦点
            return MA_NOACTIVATE;

        case WM_LBUTTONUP: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int idx = HitTest(pt);
            if (idx >= 0 && idx < (int)view_.list.size() && onSelect_) {
                onSelect_(view_.list[idx]);
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (onPage_) onPage_(delta < 0 ? +1 : -1);
            return 0;
        }

        case WM_TIMER:
            if (wParam == kToastTimerId) {
                KillTimer(hwnd, kToastTimerId);
                Hide();
            }
            return 0;

        case WM_DESTROY:
            // 用 WndProc 传入的真实 hwnd 交给 DefWindowProc，保证 WM_NCDESTROY 默认清理执行；
            // 不在此处置空 hwnd_，避免后续 DefWindowProc 收到 NULL 句柄。
            break;
    }
    // 始终用 WndProc 传入的真实 hwnd（此时成员 hwnd_ 可能已在析构路径中被改动）
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

SIZE CandidateWindowController::Measure() {
    // 用一个临时 DC + GDI+ 测量文本尺寸。此处给出布局公式，具体像素以实际字体度量为准。
    const auto& ap = config_.theme.appearance(darkMode_);
    SIZE sz = {0, 0};

    HDC hdc = GetDC(hwnd_);
    Graphics g(hdc);
    FontFamily ff(U8ToU16(ap.font_name == "system" ? "Microsoft YaHei" : ap.font_name).c_str());
    Font font(&ff, ap.font_size, FontStyleRegular, UnitPixel);

    auto measure = [&](const std::wstring& t) -> SizeF {
        RectF box;
        g.MeasureString(t.c_str(), (int)t.size(), &font, PointF(0, 0), &box);
        return SizeF(box.Width, box.Height);
    };

    float padL = ap.window_padding_left, padR = ap.window_padding_right;
    float padT = ap.window_padding_top, padB = ap.window_padding_bottom;

    // 组字区（缓存区）行
    SizeF originSz = measure(U8ToU16(view_.original_string));
    float lineH = originSz.Height;
    float maxW = originSz.Width;

    bool horizontal = config_.candidates_direction == fire::CandidatesDirection::Horizontal;
    float x = padL, y = padT + lineH + ap.origin_candidates_space;
    float rowW = 0, totalH = y;

    candidateRects_.clear();
    for (size_t i = 0; i < view_.list.size(); ++i) {
        std::wstring label = std::to_wstring(i + 1) + L". " + U8ToU16(view_.list[i].label);
        SizeF s = measure(label);
        RECT r;
        if (horizontal) {
            r.left = (LONG)x; r.top = (LONG)y;
            r.right = (LONG)(x + s.Width); r.bottom = (LONG)(y + s.Height);
            x += s.Width + ap.candidate_space;
            rowW = x;
            totalH = y + s.Height;
        } else {
            r.left = (LONG)padL; r.top = (LONG)y;
            r.right = (LONG)(padL + s.Width); r.bottom = (LONG)(y + s.Height);
            y += s.Height + ap.candidate_space;
            rowW = (std::max)(rowW, padL + s.Width);
            totalH = y;
        }
        candidateRects_.push_back(r);
    }

    maxW = (std::max)(maxW, rowW - padL);
    sz.cx = (LONG)(maxW + padL + padR);
    sz.cy = (LONG)(totalH + padB);
    ReleaseDC(hwnd_, hdc);
    if (sz.cx < 40) sz.cx = 40;
    if (sz.cy < 24) sz.cy = 24;
    return sz;
}

POINT CandidateWindowController::ComputePosition(const SIZE& sz) {
    // 默认放在光标下方；越界则翻到上方 / 贴屏幕边
    const fire::CaretRect& c = view_.caret;
    int x = (int)c.x;
    int y = (int)(c.y + c.height) + 2;

    HMONITOR mon = MonitorFromPoint(POINT{x, y}, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfo(mon, &mi)) {
        RECT wa = mi.rcWork;
        if (x + sz.cx > wa.right) x = wa.right - sz.cx;
        if (x < wa.left) x = wa.left;
        if (y + sz.cy > wa.bottom) {
            // 翻到光标上方
            y = (int)c.y - sz.cy - 2;
            if (y < wa.top) y = wa.top;
        }
    }
    // GetMonitorInfo 失败时保留光标下方原始坐标，避免钉在屏幕左上角。
    return POINT{x, y};
}

void CandidateWindowController::PaintToGraphics(Graphics& g, const SIZE& sz) {
    const auto& ap = config_.theme.appearance(darkMode_);

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // 背景 + 圆角
    SolidBrush bg(ToColor(ap.window_background_color));
    g.Clear(Color(0, 0, 0, 0));
    {
        float r = ap.window_border_radius;
        GraphicsPath path;
        // 用完整宽高构造圆角矩形（此前 -1 会造成右/下各差 1 像素）
        RectF rc(0, 0, (REAL)sz.cx, (REAL)sz.cy);
        path.AddArc(rc.X, rc.Y, r * 2, r * 2, 180, 90);
        path.AddArc(rc.GetRight() - r * 2, rc.Y, r * 2, r * 2, 270, 90);
        path.AddArc(rc.GetRight() - r * 2, rc.GetBottom() - r * 2, r * 2, r * 2, 0, 90);
        path.AddArc(rc.X, rc.GetBottom() - r * 2, r * 2, r * 2, 90, 90);
        path.CloseFigure();
        g.FillPath(&bg, &path);
    }

    FontFamily ff(U8ToU16(ap.font_name == "system" ? "Microsoft YaHei" : ap.font_name).c_str());
    Font font(&ff, ap.font_size, FontStyleRegular, UnitPixel);

    // 组字区（缓存区）
    SolidBrush originBrush(ToColor(ap.origin_code_color));
    std::wstring origin = U8ToU16(view_.original_string);
    g.DrawString(origin.c_str(), (int)origin.size(), &font,
                 PointF((REAL)ap.window_padding_left, (REAL)ap.window_padding_top), &originBrush);

    // 候选列表（首个高亮）
    SolidBrush idxBrush(ToColor(ap.candidate_index_color));
    SolidBrush textBrush(ToColor(ap.candidate_text_color));
    SolidBrush selIdxBrush(ToColor(ap.selected_index_color));
    SolidBrush selTextBrush(ToColor(ap.selected_text_color));

    for (size_t i = 0; i < view_.list.size() && i < candidateRects_.size(); ++i) {
        const RECT& r = candidateRects_[i];
        bool selected = (i == 0);
        std::wstring index = std::to_wstring(i + 1) + L". ";
        std::wstring txt = U8ToU16(view_.list[i].label);
        // 序号用序号色，候选文本用文本色（选中态各自用高亮色）
        RectF idxBox;
        g.MeasureString(index.c_str(), (int)index.size(), &font, PointF(0, 0), &idxBox);
        g.DrawString(index.c_str(), (int)index.size(), &font,
                     PointF((REAL)r.left, (REAL)r.top),
                     selected ? &selIdxBrush : &idxBrush);
        g.DrawString(txt.c_str(), (int)txt.size(), &font,
                     PointF((REAL)r.left + idxBox.Width, (REAL)r.top),
                     selected ? &selTextBrush : &textBrush);
    }
}

void CandidateWindowController::Render(const POINT& pos, const SIZE& sz) {
    // 用 UpdateLayeredWindow 做逐像素 alpha，圆角外区域完全透明（无黑块/残影）。
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sz.cx;
    bmi.bmiHeader.biHeight = -sz.cy;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

    {
        Graphics g(memDC);
        PaintToGraphics(g, sz);
    }

    POINT ptSrc = {0, 0};
    POINT ptDst = pos;
    SIZE size = sz;
    BLENDFUNCTION blend = {0};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;  // 使用位图逐像素 alpha
    UpdateLayeredWindow(hwnd_, screenDC, &ptDst, &size, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
    lastPos_ = pos;
}

void CandidateWindowController::Show(const fire::CandidatesView& view) {
    view_ = view;
    if (!hwnd_) return;
    KillTimer(hwnd_, kToastTimerId);  // 取消可能存在的提示自动隐藏定时器
    if (view_.list.empty() && view_.original_string.empty()) {
        Hide();
        return;
    }
    darkMode_ = IsDarkMode();  // 每次显示刷新一次，绘制期间使用同一值
    SIZE sz = Measure();
    POINT pos = ComputePosition(sz);
    SetWindowPos(hwnd_, HWND_TOPMOST, pos.x, pos.y, sz.cx, sz.cy,
                 SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE);
    Render(pos, sz);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    visible_ = true;
}

void CandidateWindowController::Hide() {
    if (hwnd_ && visible_) {
        KillTimer(hwnd_, kToastTimerId);
        ShowWindow(hwnd_, SW_HIDE);
        visible_ = false;
    }
}

void CandidateWindowController::ShowToast(const std::string& label) {
    // 把中英文标签作为一个单独提示显示在候选窗位置，短暂后自动隐藏。
    fire::CandidatesView v;
    v.original_string = label;
    v.caret = view_.caret;
    Show(v);
    if (hwnd_) SetTimer(hwnd_, kToastTimerId, 800, nullptr);  // 800ms 后自动隐藏
}

int CandidateWindowController::HitTest(POINT pt) const {
    for (size_t i = 0; i < candidateRects_.size(); ++i) {
        const RECT& r = candidateRects_[i];
        if (pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom) {
            return (int)i;
        }
    }
    return -1;
}

}  // namespace firewin
