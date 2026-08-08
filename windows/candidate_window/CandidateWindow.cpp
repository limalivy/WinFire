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

// 是否处于反查模式（z_key_query 开启且 original 以 ` 开头）。
// 与 InputEngine::is_reverse_lookup_mode 判定一致（反查仅发生在 ZhHans 模式下，
// 候选窗只在 ZhHans 下收到此类视图，故无需再判 input_mode）。
static bool IsReverseLookup(const fire::Config& config, const std::string& original) {
    return config.z_key_query &&
           !original.empty() && original[0] == '`';
}

// 是否应当显示编码提示：
//   - wubi_code_tip 开关开启（用户设置），或
//   - 反查模式强制显示，不受开关控制。
static bool ShouldShowCode(const fire::Config& config, const std::string& original) {
    return config.wubi_code_tip || IsReverseLookup(config, original);
}

CandidateWindowController::CandidateWindowController(const fire::Config& config)
    : config_(config) {}

CandidateWindowController::~CandidateWindowController() {
    Destroy();
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

    // 首次创建用 nullptr owner（Activate 时还没有宿主活动视图窗口信息）。
    // Reparent 会在首次 show 前（owner 已知）按需销毁重建为带 owner 的窗口。
    if (!CreateWindowOwned(nullptr)) {
        FIRE_LOG(L"[WinFire] CandidateWindow: CreateWindowExW FAILED err=%lu\n", GetLastError());
        return false;
    }
    FIRE_LOG(L"[WinFire] CandidateWindow: CreateWindowExW OK hwnd=%p\n", (void*)hwnd_);
    FIRE_LOG_EXIT();
    return true;
}

HWND CandidateWindowController::CreateWindowOwned(HWND owner) {
    // 复用 Create() 已完成的 GdiplusStartup + RegisterClassExW，只创建窗口。
    // 无焦点浮窗：TOPMOST + NOACTIVATE + TOOLWINDOW + LAYERED（用 UpdateLayeredWindow 逐像素透明）
    // owner 作为 hWndParent：对 WS_POPUP 即 owner（非 child parent）。
    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        kClassName, L"", WS_POPUP,
        0, 0, 10, 10, owner, nullptr, hInst_, this);
    ownerHwnd_ = owner;  // 记录本次创建的 owner
    return hwnd_;
}

void CandidateWindowController::Destroy() {
    FIRE_LOG_ENTER();
    // 必须在 GdiplusShutdown 之前释放所有 GDI+ 对象，否则后续成员析构
    //（unique_ptr 自动释放 cachedFont_/cachedFontFamily_）会在 GDI+ 已关闭后操作 GDI+ 对象。
    textFont_ = {};
    indexFont_ = {};
    codeFont_ = {};
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    ownerHwnd_ = nullptr;  // 窗口销毁后 owner 同步失效
    if (gdiplusToken_) { GdiplusShutdown(gdiplusToken_); gdiplusToken_ = 0; }
    FIRE_LOG_EXIT();
}

void CandidateWindowController::Reparent(HWND owner) {
    // 仅当 owner 真正变化且窗口已创建时才重建，避免每键重复。
    if (!hwnd_ || !owner || owner == ownerHwnd_) {
        FIRE_LOG(L"[WinFire] Reparent: skip (hwnd=%p owner=%p curOwner=%p)\n",
                 (void*)hwnd_, (void*)owner, (void*)ownerHwnd_);
        return;
    }
    // 关键：不能用 SetWindowLongPtr(GWLP_HWNDPARENT) 改运行中窗口的 owner——
    // 在 SearchApp.exe/AppContainer 宿主里它返回 ERROR_INVALID_PARAMETER（实测 err=87），
    // owner 根本设不上，候选窗在沙箱合成器里不可见。改为像 weasel 那样销毁重建：
    // DestroyWindow 后用新 owner 调 CreateWindowExW，owner 在窗口创建时内置。
    HWND oldHwnd = hwnd_;
    HWND oldOwner = ownerHwnd_;
    visible_ = false;  // DestroyWindow 后可见状态丢失
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    HWND newHwnd = CreateWindowOwned(owner);
    FIRE_LOG(L"[WinFire] Reparent: REBUILD old_hwnd=%p -> new_hwnd=%p owner %p -> %p ok=%d\n",
             (void*)oldHwnd, (void*)newHwnd, (void*)oldOwner, (void*)owner, newHwnd ? 1 : 0);
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
            } else if (idx == -3 && onPage_) {
                onPage_(-1);  // 上翻页指示器
            } else if (idx == -4 && onPage_) {
                onPage_(+1);  // 下翻页指示器
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

Gdiplus::Font* CandidateWindowController::GetCachedFont(int which, float dpi) {
    const auto& ap = config_.theme.appearance(darkMode_);
    // 业火 "system" 或空字串 → 默认中文字体（Microsoft YaHei）。
    // 显式字体名不可用时也回退 YaHei，避免 FontFamily 构造失败导致 MeasureString/DrawString
    // 返回零尺寸（候选窗缩小为只剩 padding + 页面指示器、且无文字绘制）。
    auto resolveName = [](const std::string& n) -> std::string {
        if (n.empty() || n == "system") return "Microsoft YaHei";
        return n;
    };
    std::string fontName = resolveName(ap.font_name);
    // which: 0=text(font_size) 1=index(index_font_size) 2=code(code_font_size)
    float themeSize = (which == 1) ? ap.index_font_size : (which == 2) ? ap.code_font_size : ap.font_size;
    float fontSize = themeSize * dpi;
    FontCache* fc = (which == 1) ? &indexFont_ : (which == 2) ? &codeFont_ : &textFont_;
    if (fc->matches(fontName, fontSize, dpi)) return fc->font.get();
    fc->family = std::make_unique<Gdiplus::FontFamily>(U8ToU16(fontName).c_str());
    // 指定字体不存在（FontFamily 不可用）→ 回退 Microsoft YaHei
    if (!fc->family->IsAvailable()) {
        fontName = "Microsoft YaHei";
        fc->family = std::make_unique<Gdiplus::FontFamily>(U8ToU16(fontName).c_str());
    }
    fc->font = std::make_unique<Gdiplus::Font>(fc->family.get(), fontSize,
                                               Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    fc->name = fontName;
    fc->pixelSize = fontSize;
    fc->dpi = dpi;
    return fc->font.get();
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
    Font* textFont = GetCachedFont(0, dpi);   // 候选文本（font_size）
    Font* indexFont = GetCachedFont(1, dpi);  // 序号（index_font_size）
    Font* codeFont = GetCachedFont(2, dpi);   // 编码提示（code_font_size）

    auto measure = [&g](const std::wstring& t, Font* f) -> SizeF {
        RectF box;
        g.MeasureString(t.c_str(), (int)t.size(), f, PointF(0, 0), &box);
        return SizeF(box.Width, box.Height);
    };

    float padL = ap.window_padding_left * dpi;
    float padR = ap.window_padding_right * dpi;
    float padT = ap.window_padding_top * dpi;
    float padB = ap.window_padding_bottom * dpi;
    float originPadT = ap.origin_padding_top * dpi;
    float originPadL = ap.origin_padding_left * dpi;
    float originPadR = ap.origin_padding_right * dpi;
    float originPadB = ap.origin_padding_bottom * dpi;
    float candPadT = ap.candidate_padding_top * dpi;
    float candPadL = ap.candidate_padding_left * dpi;
    float candPadR = ap.candidate_padding_right * dpi;
    float candPadB = ap.candidate_padding_bottom * dpi;

    // 组字区（缓存区）行
    SizeF originSz = measure(U8ToU16(view_.original_string), textFont);
    float lineH = originSz.Height;
    float maxW = originSz.Width + originPadL + originPadR;

    bool horizontal = config_.candidates_direction == fire::CandidatesDirection::Horizontal;
    float candSpace = ap.candidate_space * dpi;
    float originSpace = ap.origin_candidates_space * dpi;
    float x = padL, y = padT + originPadT + lineH + originPadB + originSpace;
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
        SizeF idxSz = measure(index, indexFont);
        SizeF txtSz = measure(txt, textFont);
        SizeF codeSz = codeHint.empty() ? SizeF(0, 0) : measure(codeHint, codeFont);
        float innerW = idxSz.Width + txtSz.Width + codeSz.Width;
        float totalW = candPadL + innerW + candPadR;
        float innerH = (std::max)({idxSz.Height, txtSz.Height, codeSz.Height});
        float h = candPadT + innerH + candPadB;
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

    // 菜单图标 ⚙ 仅在反查模式时追加在候选列表末尾（横向：右侧；竖向：下方独占一行），
    // 其余模式不显示（menuRect_ 留零矩形，HitTest 也不会命中）。
    if (IsReverseLookup(config_, view_.original_string)) {
        const wchar_t kMenuIcon[] = L"\u2699";
        SizeF menuSz = measure(kMenuIcon, textFont);
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
    } else {
        menuRect_ = {0, 0, 0, 0};
    }

    // 页面指示器（上下翻页箭头）：list.size()>1 || hasPrev || hasNext 时显示。
    // 业火规则：横向竖排两箭头放候选列表右侧、与候选行垂直居中（不含原码行）；
    // 竖向横排放候选列表下方。大小 = fontSize*0.5。
    bool showIndicator = view_.list.size() > 1 || view_.has_prev || view_.has_next;
    if (showIndicator) {
        float indSize = ap.font_size * 0.5f * dpi;
        float indGap = 2.0f * dpi;  // 两箭头之间留 2px
        float indTotal = 2 * indSize + indGap;  // 竖排两箭头总高
        if (horizontal) {
            // 竖排，放在当前行末尾（候选列表右侧），与候选行（不含原码）垂直居中
            float ix = (std::max)(rowW, x) + candSpace;
            float candTop = y;        // 候选行起始 y（循环只增 x，y 保持候选行顶部）
            float candBot = totalH;   // 候选行底部
            float iy = candTop + ((candBot - candTop) - indTotal) / 2.0f;
            pageUpRect_ = {(LONG)ix, (LONG)iy, (LONG)(ix + indSize), (LONG)(iy + indSize)};
            pageDownRect_ = {(LONG)ix, (LONG)(iy + indSize + indGap),
                             (LONG)(ix + indSize), (LONG)(iy + 2 * indSize + indGap)};
            rowW = (std::max)(rowW, ix + indSize);
            totalH = (std::max)(totalH, iy + indTotal);
        } else {
            // 横排，放在候选列表下方
            float ix = padL;
            float iy = totalH + candSpace;
            pageUpRect_ = {(LONG)ix, (LONG)iy, (LONG)(ix + indSize), (LONG)(iy + indSize)};
            pageDownRect_ = {(LONG)(ix + indSize + indGap), (LONG)iy,
                             (LONG)(ix + 2 * indSize + indGap), (LONG)(iy + indSize)};
            rowW = (std::max)(rowW, padL + 2 * indSize + indGap);
            totalH = iy + indSize;
        }
    } else {
        pageUpRect_ = {0, 0, 0, 0};
        pageDownRect_ = {0, 0, 0, 0};
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

    // 无光标信息（caret 全零，如全新会话语言栏切换、未显示过候选）：
    // 不要钉在 (0,2) 屏幕左上角，改为落在「鼠标所在显示器」工作区的
    // x 轴居中、y 轴 2/3 位置（视觉上贴近输入区，且不遮挡顶部内容）。
    if (c.x == 0 && c.y == 0 && c.width == 0 && c.height == 0) {
        POINT cursor = {0, 0};
        HMONITOR mon = nullptr;
        if (GetCursorPos(&cursor)) {
            mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        } else {
            mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        }
        MONITORINFO mi = {sizeof(mi)};
        if (GetMonitorInfo(mon, &mi)) {
            RECT wa = mi.rcWork;
            x = wa.left + ((wa.right - wa.left) - sz.cx) / 2;
            y = wa.top + ((wa.bottom - wa.top) * 2) / 3;
            FIRE_LOG(L"[WinFire] ComputePosition: no caret, fallback center-x 2/3-y=(%d,%d)\n", x, y);
            FIRE_LOG_EXIT();
            return POINT{x, y};
        }
        // GetMonitorInfo 也失败（极罕见）：保留 (0,2)，至少不崩。
        FIRE_LOG_EXIT();
        return POINT{x, y};
    }

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

    Font* textFont = GetCachedFont(0, dpi);   // 候选文本（font_size）
    Font* indexFont = GetCachedFont(1, dpi);  // 序号（index_font_size）
    Font* codeFont = GetCachedFont(2, dpi);   // 编码提示（code_font_size）

    float candPadT = ap.candidate_padding_top * dpi;
    float candPadL = ap.candidate_padding_left * dpi;

    // 组字区（缓存区）
    SolidBrush originBrush(ToColor(ap.origin_code_color));
    std::wstring origin = U8ToU16(view_.original_string);
    g.DrawString(origin.c_str(), (int)origin.size(), textFont,
                 PointF((REAL)(ap.window_padding_left * dpi + ap.origin_padding_left * dpi),
                        (REAL)(ap.window_padding_top * dpi + ap.origin_padding_top * dpi)),
                 &originBrush);

    // 候选列表（首个高亮）
    SolidBrush idxBrush(ToColor(ap.candidate_index_color));
    SolidBrush textBrush(ToColor(ap.candidate_text_color));
    SolidBrush codeBrush(ToColor(ap.candidate_code_color));
    SolidBrush selIdxBrush(ToColor(ap.selected_index_color));
    SolidBrush selTextBrush(ToColor(ap.selected_text_color));
    SolidBrush selCodeBrush(ToColor(ap.selected_code_color));
    // 选中项圆角背景（非全透时才画）
    SolidBrush selBgBrush(ToColor(ap.selected_background_color));
    bool drawSelBg = ap.selected_background_color.opacity > 0.0001;
    float candRadius = ap.candidate_radius * dpi;

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
        // 选中项背景圆角矩形（先画背景，再画文字，避免覆盖）
        if (selected && drawSelBg) {
            GraphicsPath bp;
            RectF br((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
            float rr = (std::min)(candRadius, (std::min)(br.Width, br.Height) / 2.0f);
            bp.AddArc(br.X, br.Y, rr * 2, rr * 2, 180, 90);
            bp.AddArc(br.GetRight() - rr * 2, br.Y, rr * 2, rr * 2, 270, 90);
            bp.AddArc(br.GetRight() - rr * 2, br.GetBottom() - rr * 2, rr * 2, rr * 2, 0, 90);
            bp.AddArc(br.X, br.GetBottom() - rr * 2, rr * 2, rr * 2, 90, 90);
            bp.CloseFigure();
            g.FillPath(&selBgBrush, &bp);
        }
        // 按顺序绘制：序号 → 文本 → 编码提示，各段用对应字号与颜色
        RectF idxBox, txtBox;
        g.MeasureString(index.c_str(), (int)index.size(), indexFont, PointF(0, 0), &idxBox);
        g.MeasureString(txt.c_str(), (int)txt.size(), textFont, PointF(0, 0), &txtBox);
        float curX = (REAL)r.left + candPadL;
        float curY = (REAL)r.top + candPadT;
        g.DrawString(index.c_str(), (int)index.size(), indexFont,
                     PointF(curX, curY),
                     selected ? &selIdxBrush : &idxBrush);
        curX += idxBox.Width;
        g.DrawString(txt.c_str(), (int)txt.size(), textFont,
                     PointF(curX, curY),
                     selected ? &selTextBrush : &textBrush);
        curX += txtBox.Width;
        if (!codeHint.empty()) {
            g.DrawString(codeHint.c_str(), (int)codeHint.size(), codeFont,
                         PointF(curX, curY),
                         selected ? &selCodeBrush : &codeBrush);
        }
    }

    // 菜单图标 ⚙ 仅在反查模式下绘制（Measure 中非反查模式已留零矩形）
    if (IsReverseLookup(config_, view_.original_string)) {
        const wchar_t kMenuIcon[] = L"\u2699";
        SolidBrush menuBrush(ToColor(ap.candidate_index_color));
        g.DrawString(kMenuIcon, 1, textFont,
                     PointF((REAL)menuRect_.left, (REAL)menuRect_.top),
                     &menuBrush);
    }

    // 页面指示器（上下翻页箭头）。Measure 已在显示时填好 pageUpRect_/pageDownRect_。
    // 业火用 template image 渲染纯色；此处用等价的纯色三角形自绘，无需引入 PNG 资源。
    auto drawArrow = [&](const RECT& r, bool up, bool disabled) {
        if (r.right <= r.left || r.bottom <= r.top) return;
        const Color& c = disabled ? ToColor(ap.page_indicator_disabled_color)
                                  : ToColor(ap.page_indicator_color);
        SolidBrush br(c);
        GraphicsPath p;
        REAL cx = ((REAL)r.left + r.right) / 2.0f;
        if (up) {
            p.AddLine((REAL)r.left, (REAL)r.bottom, cx, (REAL)r.top);
            p.AddLine(cx, (REAL)r.top, (REAL)r.right, (REAL)r.bottom);
            p.AddLine((REAL)r.right, (REAL)r.bottom, (REAL)r.left, (REAL)r.bottom);
        } else {
            p.AddLine((REAL)r.left, (REAL)r.top, (REAL)r.right, (REAL)r.top);
            p.AddLine((REAL)r.right, (REAL)r.top, cx, (REAL)r.bottom);
            p.AddLine(cx, (REAL)r.bottom, (REAL)r.left, (REAL)r.top);
        }
        p.CloseFigure();
        g.FillPath(&br, &p);
    };
    bool showIndicator = view_.list.size() > 1 || view_.has_prev || view_.has_next;
    if (showIndicator) {
        drawArrow(pageUpRect_, true, !view_.has_prev);
        drawArrow(pageDownRect_, false, !view_.has_next);
    }
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
    if (!memDC) {
        FIRE_LOG(L"[WinFire] Render: CreateCompatibleDC FAILED err=%lu\n", GetLastError());
        ReleaseDC(nullptr, screenDC);
        FIRE_LOG_EXIT();
        return;
    }

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sz.cx;
    bmi.bmiHeader.biHeight = -sz.cy;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp) {
        // 极端低内存或尺寸非法：跳过本次绘制，避免在无效 DC/bitmap 上操作。
        FIRE_LOG(L"[WinFire] Render: CreateDIBSection FAILED err=%lu\n", GetLastError());
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        FIRE_LOG_EXIT();
        return;
    }
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
    darkMode_ = ResolveDarkMode();  // 按 dark_mode_preference + 系统深浅色解析
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
    // 旧重载：拷贝陈旧 view_.caret（无新鲜光标信息时的兜底）。
    ShowToast(label, view_.caret);
}

void CandidateWindowController::ShowToast(const std::string& label, const fire::CaretRect& caret) {
    FIRE_LOG_ENTER();
    FIRE_LOG(L"[WinFire] ShowToast: label='%hs' caret=(%d,%d,%dx%d)\n",
             label.c_str(), (int)caret.x, (int)caret.y, (int)caret.width, (int)caret.height);
    // 把中英文标签作为一个单独提示显示，定位锚点用调用方传入的新鲜光标
    //（而非陈旧的 view_.caret，后者在尚未显示过候选时是 {0,0,0,0}，
    // 会把提示钉到屏幕左上角）。短暂后自动隐藏。
    fire::CandidatesView v;
    v.original_string = label;
    v.caret = caret;
    Show(v);
    if (hwnd_) SetTimer(hwnd_, kToastTimerId, 800, nullptr);  // 800ms 后自动隐藏
    FIRE_LOG_EXIT();
}

int CandidateWindowController::HitTest(POINT pt) const {
    // 页面指示器优先判定（在候选矩形之前），命中时返回 -3(上翻)/-4(下翻)。
    // 不可用方向（!has_prev/!has_next）的矩形不命中，与业火点击禁用语义一致。
    auto inRect = [&](const RECT& r) {
        return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
    };
    if (pageUpRect_.right > pageUpRect_.left && view_.has_prev && inRect(pageUpRect_)) return -3;
    if (pageDownRect_.right > pageDownRect_.left && view_.has_next && inRect(pageDownRect_)) return -4;
    for (size_t i = 0; i < candidateRects_.size(); ++i) {
        const RECT& r = candidateRects_[i];
        if (inRect(r)) {
            return (int)i;
        }
    }
    // 菜单图标命中（仅反查模式下 menuRect_ 非空；零矩形时跳过，避免误命中左上角）
    if (menuRect_.right > menuRect_.left && menuRect_.bottom > menuRect_.top && inRect(menuRect_)) {
        return -2;
    }
    return -1;
}

void CandidateWindowController::LaunchConfigTool() {
    // fire_tsf.dll 与 fire_config.exe 同目录：获取本 DLL 路径后替换末段文件名。
    // hInst_ 是创建候选窗时传入的本模块句柄（TSF DLL 的 g_hInst）。不能按固定名
    // GetModuleHandleW(L"fire_tsf.dll") 查找——DLL 已改为版本化文件名（如
    // fire_tsf_0.1.0.dll），固定名会查不到。
    wchar_t dllPath[MAX_PATH] = {0};
    if (!hInst_ || !GetModuleFileNameW(hInst_, dllPath, MAX_PATH)) {
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

bool CandidateWindowController::ResolveDarkMode() const {
    // dark_mode_preference：0=跟随系统，1=强制浅色，2=强制深色。
    int pref = config_.theme.dark_mode_preference;
    if (pref == 1) return false;
    if (pref == 2) return true;
    // 跟随系统：读 HKCU\...\Themes\Personalize\AppsUseLightTheme（0=深色，1=浅色）。
    // 仅用当前用户注册表（候选窗运行在用户会话，HKCU 可靠）。读取失败默认浅色。
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD value = 1, size = sizeof(value), type = 0;
        BOOL light = TRUE;
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, &type,
                             (BYTE*)&value, &size) == ERROR_SUCCESS && type == REG_DWORD) {
            light = (value != 0);
        }
        RegCloseKey(hKey);
        return !light;
    }
    return false;
}

}  // namespace firewin
