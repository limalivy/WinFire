//
//  CandidateWindow.h — 无焦点候选浮窗（Win32 + GDI+ 自绘）
//
//  设计要点：
//    - WS_POPUP + WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW，避免抢焦点
//    - WM_MOUSEACTIVATE 返回 MA_NOACTIVATE
//    - GDI+ 绘制组字区(缓存区) + 候选列表(横/竖) + 序号/编码 + 翻页指示
//    - 主题色取自 fire::ThemeConfig
//
#pragma once

#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fire/config.h"
#include "fire/candidate.h"
#include "fire/input_client.h"  // CandidatesView / CaretRect

namespace Gdiplus { class Graphics; class Font; class FontFamily; }

namespace firewin {

class CandidateWindowController {
public:
    explicit CandidateWindowController(const fire::Config& config);
    ~CandidateWindowController();

    bool Create(HINSTANCE hInst);
    void Destroy();

    // 重新设置候选窗的 owner（对 WS_POPUP 即 owner）。
    // SearchHost.exe / UWP 等 AppContainer 沙箱宿主下，无 owner 的顶层工具窗
    // 会落在沉浸式 UI 的合成器之外，候选框不可见；必须以宿主活动视图窗口为
    // owner（参考 weasel _GetActiveWnd）。实现方式：owner 变化时销毁原窗口、
    // 用新 owner 重新 CreateWindowExW（SetWindowLongPtr 改运行中窗口的 owner
    // 在 AppContainer 宿主下会失败）。
    void Reparent(HWND owner);

    // 显示/更新候选窗
    void Show(const fire::CandidatesView& view);
    void Hide();

    // 中英文切换提示（短暂显示）。
    // 有光标信息时优先用带 caret 的重载，避免拷贝陈旧 view_.caret 导致定位到
    // 屏幕左上角；无 caret 信息时退回旧重载（拷贝 view_.caret）。
    void ShowToast(const std::string& label);
    void ShowToast(const std::string& label, const fire::CaretRect& caret);

    // 回调：点击候选 / 翻页（delta>0 下一页）
    void SetOnSelect(std::function<void(const fire::Candidate&)> cb) { onSelect_ = std::move(cb); }
    void SetOnPage(std::function<void(int)> cb) { onPage_ = std::move(cb); }

private:
    const fire::Config& config_;
    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND ownerHwnd_ = nullptr;  // 当前 owner，Reparent 仅在变化时才销毁重建
    ULONG_PTR gdiplusToken_ = 0;

    fire::CandidatesView view_;
    std::vector<RECT> candidateRects_;  // 命中测试用
    RECT menuRect_ = {0, 0, 0, 0};      // ⚙ 菜单图标命中区域
    bool visible_ = false;
    bool darkMode_ = false;             // 主题未适配深色模式，固定 false（用 light 配色）
    POINT lastPos_ = {0, 0};            // 上次窗口左上角（UpdateLayeredWindow 需要）

    // Font 缓存：Measure + PaintToGraphics 共用，避免每次候选刷新重建 4 个 GDI+ 对象。
    // 仅在 (font_name, font_size, dpi) 变化时重建。
    std::unique_ptr<Gdiplus::FontFamily> cachedFontFamily_;
    std::unique_ptr<Gdiplus::Font> cachedFont_;
    std::string cachedFontName_;        // 已映射后的物理字体名（"system"→"Microsoft YaHei"）
    float cachedFontSize_ = 0;
    float cachedDpi_ = 0;
    // 按 dpi 取缓存的 Font（font_name/size 取自当前 theme）。失效则重建。
    Gdiplus::Font* GetCachedFont(float dpi);

    std::function<void(const fire::Candidate&)> onSelect_;
    std::function<void(int)> onPage_;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 用 UpdateLayeredWindow 渲染窗口（支持圆角逐像素透明）
    void Render(const POINT& pos, const SIZE& sz);
    void PaintToGraphics(Gdiplus::Graphics& g, const SIZE& sz);  // 在离屏 Graphics 上绘制内容
    SIZE Measure();               // 依据 view_ 计算窗口尺寸
    POINT ComputePosition(const SIZE& sz);  // 依据 caret 计算窗口左上角
    int HitTest(POINT pt) const;      // 返回候选索引，-1 未命中，-2 命中菜单图标
    void LaunchConfigTool();          // 启动 fire_config.exe

    // 仅创建窗口（CreateWindowExW），owner 作为 hWndParent 传入；GDI+/RegisterClass 由
    // Create() 一次性完成，本方法只复用已注册的类。供 Create 与 Reparent(销毁重建) 共用。
    HWND CreateWindowOwned(HWND owner);

    // 获取当前窗口所在显示器的 DPI 缩放因子（1.0 = 96 DPI）
    // 用于在高 DPI 显示器（4K/Retina）上等比放大字体与间距。
    float GetDpiScale() const;

    // 中英文提示自动隐藏定时器 ID
    enum { kToastTimerId = 1 };
};

}  // namespace firewin
