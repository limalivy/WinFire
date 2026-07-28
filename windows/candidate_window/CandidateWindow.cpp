//
//  CandidateWindow.cpp — Win32 + GDI+ 自绘候选窗
//
#include "CandidateWindow.h"
#include "../tsf/DebugLog.h"

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

// 计算候选词的编码提示文本，对应 Swift CandidatesView.swift getShownCode。
// 规则：
//   - 拼音候选 或 code 不是以 origin 开头：显示 (code)
//   - code 比 origin 长：显示后缀（不含 ~ 前缀）
//   - 否则：空串（不显示）
// 反查模式下候选 code 是五笔码、origin 是 `拼音，不以 origin 开头，会显示 (code)。
static std::string GetShownCode(const fire::Candidate& cand, const std::string& origin) {
    if (cand.type == fire::CandidateType::Py ||
        cand.code.size() < origin.size() ||
        cand.code.compare(0, origin.size(), origin) != 0) {
        return "(" + cand.code + ")";
    }
    if (cand.code.size() > origin.size()) {
        return cand.code.substr(origin.size());
    }
    return std::string();
}

// 是否应当显示编码提示：
//   - wubi_code_tip 开关开启（用户设置），或
//   - 反查模式（z_key_query 开启且 original 以 ` 开头）强制显示，不受开关控制。
// 与 InputEngine::is_reverse_lookup_mode 判定一致（反查仅发生在 ZhHans 模式下，
// 候选窗只在 ZhHans 下收到此类视图，故无需再判 input_mode）。
static bool ShouldShowCode(const fire::Config& config, const std::string& original) {
    bool reverseLookup = config.z_key_query &&
                         !original.empty() && original[0] == '`';
    return config.wubi_code_tip || reverseLookup;
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

float CandidateWindowController::GetDpiScale() const {
    // GetDpiForWindow 需 Windows 10 1607+。动态绑定避免在老系统上加载失败。
    // DPI=96 时缩放因子为 1.0；150% 缩放 DPI=144，缩放因子 1.5；200% 缩放 DPI=192，缩放因子 2.0。
    if (!hwnd_) return 1.0f;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
        auto pfn = reinterpret_cast<PFN_GetDpiForWindow>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (pfn) {
            UINT dpi = pfn(hwnd_);
            if (dpi > 0) return dpi / 96.0f;
        }
    }
    // 回退：按系统 DPI（SystemDpi）估算，兼顾无 GetDpiForWindow 的旧系统。
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        if (dpi > 0) return dpi / 96.0f;
    }
    return 1.0f;
}

bool CandidateWindowController::Create(HINSTANCE hInst) {
    FIRE_LOG_ENTER();
    hInst_ = hInst;
    FIRE_LOG(L"[WinFire] CandidateWindow::Create hInst=%p\n", (void*)hInst);

    GdiplusStartupInput gi;
    if (GdiplusStartup(&gdiplusToken_, &gi, nullptr) != Gdiplus::Ok) {
        FIRE_LOG(L"[WinFire] CandidateWindow: GdiplusStartup FAILED\n");
        gdiplusToken_ = 0;
        return false;
    }
    FIRE_LOG(L"[WinFire] CandidateWindow: GdiplusStartup OK token=%lu\n", (unsigned long)gdiplusToken_);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    // RegisterClassExW 可能因类已注册（同一进程重复激活）返回 0，不算失败。
    ATOM atom = RegisterClassExW(&wc);
    FIRE_LOG(L"[WinFire] CandidateWindow: RegisterClassExW atom=%u\n", (unsigned)atom);

    // 无焦点浮窗：TOPMOST + NOACTIVATE + TOOLWINDOW + LAYERED（用 UpdateLayeredWindow 逐像素透明）
    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        kClassName, L"", WS_POPUP,
        0, 0, 10, 10, nullptr, nullptr, hInst, this);
    if (!hwnd_) {
        FIRE_LOG(L"[WinFire] CandidateWindow: CreateWindowExW FAILED err=%lu\n", GetLastError());
        return false;
    }
    FIRE_LOG(L"[WinFire] CandidateWindow: CreateWindowExW OK hwnd=%p\n", (void*)hwnd_);
    FIRE_LOG_EXIT();
    return true;
}

void CandidateWindowController::Destroy() {
    FIRE_LOG_ENTER();
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    if (gdiplusToken_) { GdiplusShutdown(gdiplusToken_); gdiplusToken_ = 0; }
    FIRE_LOG_EXIT();
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
            FIRE_LOG(L"[WinFire] WM_LBUTTONUP: idx=%d list_size=%zu\n", idx, view_.list.size());
            if (idx == -2) {
                LaunchConfigTool();
            } else if (idx >= 0 && idx < (int)view_.list.size() && onSelect_) {
                onSelect_(view_.list[idx]);
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            FIRE_LOG(L"[WinFire] WM_MOUSEWHEEL: delta=%d\n", (int)delta);
            if (onPage_) onPage_(delta < 0 ? +1 : -1);
            return 0;
        }

        case WM_TIMER:
            if (wParam == kToastTimerId) {
                FIRE_LOG(L"[WinFire] WM_TIMER: toast auto-hide\n");
                KillTimer(hwnd, kToastTimerId);
                Hide();
            }
            return 0;

        case WM_DESTROY:
            FIRE_LOG(L"[WinFire] WM_DESTROY: hwnd=%p\n", (void*)hwnd);
            // 用 WndProc 传入的真实 hwnd 交给 DefWindowProc，保证 WM_NCDESTROY 默认清理执行；
            // 不在此处置空 hwnd_，避免后续 DefWindowProc 收到 NULL 句柄。
            break;
    }
    // 始终用 WndProc 传入的真实 hwnd（此时成员 hwnd_ 可能已在析构路径中被改动）
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

SIZE CandidateWindowController::Measure() {
    FIRE_LOG_ENTER();
    // 用一个临时 DC + GDI+ 测量文本尺寸。此处给出布局公式，具体像素以实际字体度量为准。
    const auto& ap = config_.theme.appearance(darkMode_);
    const float dpi = GetDpiScale();  // 高 DPI 显示器等比放大
    FIRE_LOG(L"[WinFire] Measure: dpi_scale=%.2f\n", dpi);
    SIZE sz = {0, 0};

    HDC hdc = GetDC(hwnd_);
    if (!hdc) {
        FIRE_LOG(L"[WinFire] Measure: GetDC FAILED err=%lu\n", GetLastError());
        FIRE_LOG_EXIT();
        return sz;
    }
    Graphics g(hdc);
    FontFamily ff(U8ToU16(ap.font_name == "system" ? "Microsoft YaHei" : ap.font_name).c_str());
    Font font(&ff, ap.font_size * dpi, FontStyleRegular, UnitPixel);

    auto measure = [&](const std::wstring& t) -> SizeF {
        RectF box;
        g.MeasureString(t.c_str(), (int)t.size(), &font, PointF(0, 0), &box);
        return SizeF(box.Width, box.Height);
    };

    float padL = ap.window_padding_left * dpi;
    float padR = ap.window_padding_right * dpi;
    float padT = ap.window_padding_top * dpi;
    float padB = ap.window_padding_bottom * dpi;

    // 组字区（缓存区）行
    SizeF originSz = measure(U8ToU16(view_.original_string));
    float lineH = originSz.Height;
    float maxW = originSz.Width;

    bool horizontal = config_.candidates_direction == fire::CandidatesDirection::Horizontal;
    float candSpace = ap.candidate_space * dpi;
    float originSpace = ap.origin_candidates_space * dpi;
    float x = padL, y = padT + lineH + originSpace;
    float rowW = 0, totalH = y;

    candidateRects_.clear();
    for (size_t i = 0; i < view_.list.size(); ++i) {
        // 布局：序号 + 文本 [+ 编码提示]，三段拼接测量，绘制时按相同顺序与颜色分段绘制
        std::wstring index = std::to_wstring(i + 1) + L". ";
        std::wstring txt = U8ToU16(view_.list[i].label);
        std::wstring codeHint;
        if (ShouldShowCode(config_, view_.original_string)) {
            std::string hint = GetShownCode(view_.list[i], view_.original_string);
            if (!hint.empty()) codeHint = L" " + U8ToU16(hint);
        }
        SizeF idxSz = measure(index);
        SizeF txtSz = measure(txt);
        SizeF codeSz = codeHint.empty() ? SizeF(0, 0) : measure(codeHint);
        float totalW = idxSz.Width + txtSz.Width + codeSz.Width;
        float h = (std::max)({idxSz.Height, txtSz.Height, codeSz.Height});
        RECT r;
        if (horizontal) {
            r.left = (LONG)x; r.top = (LONG)y;
            r.right = (LONG)(x + totalW); r.bottom = (LONG)(y + h);
            x += totalW + candSpace;
            rowW = x;
            totalH = y + h;
        } else {
            r.left = (LONG)padL; r.top = (LONG)y;
            r.right = (LONG)(padL + totalW); r.bottom = (LONG)(y + h);
            y += h + candSpace;
            rowW = (std::max)(rowW, padL + totalW);
            totalH = y;
        }
        candidateRects_.push_back(r);
    }

    // 菜单图标 ⚙ 追加在候选列表末尾（横向：右侧；竖向：下方独占一行）
    {
        const wchar_t kMenuIcon[] = L"\u2699";
        SizeF menuSz = measure(kMenuIcon);
        RECT r;
        if (horizontal) {
            r.left = (LONG)x; r.top = (LONG)y;
            r.right = (LONG)(x + menuSz.Width);
            r.bottom = (LONG)(y + menuSz.Height);
            rowW = (std::max)(rowW, x + menuSz.Width);
            totalH = (std::max)(totalH, y + menuSz.Height);
        } else {
            r.left = (LONG)padL; r.top = (LONG)y;
            r.right = (LONG)(padL + menuSz.Width);
            r.bottom = (LONG)(y + menuSz.Height);
            rowW = (std::max)(rowW, padL + menuSz.Width);
            totalH = y + menuSz.Height;
        }
        menuRect_ = r;
    }

    maxW = (std::max)(maxW, rowW - padL);
    sz.cx = (LONG)(maxW + padL + padR);
    sz.cy = (LONG)(totalH + padB);
    ReleaseDC(hwnd_, hdc);
    if (sz.cx < 40) sz.cx = 40;
    if (sz.cy < 24) sz.cy = 24;
    FIRE_LOG(L"[WinFire] Measure: size=(%ld,%ld)\n", (long)sz.cx, (long)sz.cy);
    FIRE_LOG_EXIT();
    return sz;
}

POINT CandidateWindowController::ComputePosition(const SIZE& sz) {
    FIRE_LOG_ENTER();
    // 默认放在光标下方；越界则翻到上方 / 贴屏幕边
    const fire::CaretRect& c = view_.caret;
    int x = (int)c.x;
    int y = (int)(c.y + c.height) + 2;
    FIRE_LOG(L"[WinFire] ComputePosition: caret=(%d,%d,%dx%d) win_size=(%ld,%ld)\n",
             c.x, c.y, c.width, c.height, (long)sz.cx, (long)sz.cy);

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
    FIRE_LOG(L"[WinFire] ComputePosition: final=(%d,%d)\n", x, y);
    FIRE_LOG_EXIT();
    return POINT{x, y};
}

void CandidateWindowController::PaintToGraphics(Graphics& g, const SIZE& sz) {
    const auto& ap = config_.theme.appearance(darkMode_);
    const float dpi = GetDpiScale();  // 与 Measure 保持一致的 DPI 缩放

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // 背景 + 圆角
    SolidBrush bg(ToColor(ap.window_background_color));
    g.Clear(Color(0, 0, 0, 0));
    {
        float r = ap.window_border_radius * dpi;
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
    Font font(&ff, ap.font_size * dpi, FontStyleRegular, UnitPixel);

    // 组字区（缓存区）
    SolidBrush originBrush(ToColor(ap.origin_code_color));
    std::wstring origin = U8ToU16(view_.original_string);
    g.DrawString(origin.c_str(), (int)origin.size(), &font,
                 PointF((REAL)(ap.window_padding_left * dpi),
                        (REAL)(ap.window_padding_top * dpi)),
                 &originBrush);

    // 候选列表（首个高亮）
    SolidBrush idxBrush(ToColor(ap.candidate_index_color));
    SolidBrush textBrush(ToColor(ap.candidate_text_color));
    SolidBrush codeBrush(ToColor(ap.candidate_code_color));
    SolidBrush selIdxBrush(ToColor(ap.selected_index_color));
    SolidBrush selTextBrush(ToColor(ap.selected_text_color));
    SolidBrush selCodeBrush(ToColor(ap.selected_code_color));

    for (size_t i = 0; i < view_.list.size() && i < candidateRects_.size(); ++i) {
        const RECT& r = candidateRects_[i];
        bool selected = (i == 0);
        std::wstring index = std::to_wstring(i + 1) + L". ";
        std::wstring txt = U8ToU16(view_.list[i].label);
        // 编码提示（与 Measure 保持一致：wubi_code_tip 开启或反查模式时附加在文本后）
        std::wstring codeHint;
        if (ShouldShowCode(config_, view_.original_string)) {
            std::string hint = GetShownCode(view_.list[i], view_.original_string);
            if (!hint.empty()) codeHint = L" " + U8ToU16(hint);
        }
        // 按顺序绘制：序号 → 文本 → 编码提示，各段用对应颜色
        RectF idxBox, txtBox;
        g.MeasureString(index.c_str(), (int)index.size(), &font, PointF(0, 0), &idxBox);
        g.MeasureString(txt.c_str(), (int)txt.size(), &font, PointF(0, 0), &txtBox);
        float curX = (REAL)r.left;
        g.DrawString(index.c_str(), (int)index.size(), &font,
                     PointF(curX, (REAL)r.top),
                     selected ? &selIdxBrush : &idxBrush);
        curX += idxBox.Width;
        g.DrawString(txt.c_str(), (int)txt.size(), &font,
                     PointF(curX, (REAL)r.top),
                     selected ? &selTextBrush : &textBrush);
        curX += txtBox.Width;
        if (!codeHint.empty()) {
            g.DrawString(codeHint.c_str(), (int)codeHint.size(), &font,
                         PointF(curX, (REAL)r.top),
                         selected ? &selCodeBrush : &codeBrush);
        }
    }

    // 菜单图标 ⚙（用序号同色，低调显示）
    const wchar_t kMenuIcon[] = L"\u2699";
    SolidBrush menuBrush(ToColor(ap.candidate_index_color));
    g.DrawString(kMenuIcon, 1, &font,
                 PointF((REAL)menuRect_.left, (REAL)menuRect_.top),
                 &menuBrush);
}

void CandidateWindowController::Render(const POINT& pos, const SIZE& sz) {
    FIRE_LOG_ENTER();
    FIRE_LOG(L"[WinFire] Render: pos=(%ld,%ld) size=(%ld,%ld) hwnd=%p\n",
             (long)pos.x, (long)pos.y, (long)sz.cx, (long)sz.cy, (void*)hwnd_);
    // 用 UpdateLayeredWindow 做逐像素 alpha，圆角外区域完全透明（无黑块/残影）。
    HDC screenDC = GetDC(nullptr);
    if (!screenDC) {
        FIRE_LOG(L"[WinFire] Render: GetDC(nullptr) FAILED err=%lu\n", GetLastError());
        FIRE_LOG_EXIT();
        return;
    }
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
    BOOL ok = UpdateLayeredWindow(hwnd_, screenDC, &ptDst, &size, memDC, &ptSrc, 0, &blend, ULW_ALPHA);
    if (!ok) {
        FIRE_LOG(L"[WinFire] Render: UpdateLayeredWindow FAILED err=%lu\n", GetLastError());
    }

    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
    lastPos_ = pos;
    FIRE_LOG_EXIT();
}

void CandidateWindowController::Show(const fire::CandidatesView& view) {
    FIRE_LOG_ENTER();
    FIRE_LOG(L"[WinFire] Show: hwnd=%p list_size=%zu origin='%hs'\n",
             (void*)hwnd_, view.list.size(), view.original_string.c_str());
    view_ = view;
    if (!hwnd_) {
        FIRE_LOG(L"[WinFire] Show: hwnd_ null, abort\n");
        FIRE_LOG_EXIT();
        return;
    }
    KillTimer(hwnd_, kToastTimerId);  // 取消可能存在的提示自动隐藏定时器
    if (view_.list.empty() && view_.original_string.empty()) {
        FIRE_LOG(L"[WinFire] Show: empty view, calling Hide\n");
        Hide();
        FIRE_LOG_EXIT();
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
    FIRE_LOG_EXIT();
}

void CandidateWindowController::Hide() {
    FIRE_LOG_ENTER();
    FIRE_LOG(L"[WinFire] Hide: hwnd=%p visible=%d\n", (void*)hwnd_, visible_ ? 1 : 0);
    if (hwnd_ && visible_) {
        KillTimer(hwnd_, kToastTimerId);
        ShowWindow(hwnd_, SW_HIDE);
        visible_ = false;
    }
    FIRE_LOG_EXIT();
}

void CandidateWindowController::ShowToast(const std::string& label) {
    FIRE_LOG_ENTER();
    FIRE_LOG(L"[WinFire] ShowToast: label='%hs'\n", label.c_str());
    // 把中英文标签作为一个单独提示显示在候选窗位置，短暂后自动隐藏。
    fire::CandidatesView v;
    v.original_string = label;
    v.caret = view_.caret;
    Show(v);
    if (hwnd_) SetTimer(hwnd_, kToastTimerId, 800, nullptr);  // 800ms 后自动隐藏
    FIRE_LOG_EXIT();
}

int CandidateWindowController::HitTest(POINT pt) const {
    for (size_t i = 0; i < candidateRects_.size(); ++i) {
        const RECT& r = candidateRects_[i];
        if (pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom) {
            return (int)i;
        }
    }
    // 菜单图标命中
    if (pt.x >= menuRect_.left && pt.x <= menuRect_.right &&
        pt.y >= menuRect_.top && pt.y <= menuRect_.bottom) {
        return -2;
    }
    return -1;
}

void CandidateWindowController::LaunchConfigTool() {
    // fire_tsf.dll 与 fire_config.exe 同目录：获取 DLL 路径后替换末段文件名
    HMODULE hSelf = GetModuleHandleW(L"fire_tsf.dll");
    wchar_t dllPath[MAX_PATH] = {0};
    if (!hSelf || !GetModuleFileNameW(hSelf, dllPath, MAX_PATH)) {
        FIRE_LOG(L"[WinFire] LaunchConfigTool: GetModuleFileName FAILED err=%lu\n", GetLastError());
        return;
    }
    std::wstring path(dllPath);
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return;
    path = path.substr(0, pos + 1) + L"fire_config.exe";
    HINSTANCE h = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    FIRE_LOG(L"[WinFire] LaunchConfigTool: launch '%ls' hinst=%p err=%lu\n",
             path.c_str(), (void*)h, GetLastError());
}

}  // namespace firewin
