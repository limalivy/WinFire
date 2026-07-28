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
#include <string>
#include <vector>

#include "fire/config.h"
#include "fire/candidate.h"
#include "fire/input_client.h"  // CandidatesView / CaretRect

namespace Gdiplus { class Graphics; }

namespace firewin {

class CandidateWindowController {
public:
    explicit CandidateWindowController(const fire::Config& config);
    ~CandidateWindowController();

    bool Create(HINSTANCE hInst);
    void Destroy();

    // 显示/更新候选窗
    void Show(const fire::CandidatesView& view);
    void Hide();

    // 中英文切换提示（短暂显示）
    void ShowToast(const std::string& label);

    // 回调：点击候选 / 翻页（delta>0 下一页）
    void SetOnSelect(std::function<void(const fire::Candidate&)> cb) { onSelect_ = std::move(cb); }
    void SetOnPage(std::function<void(int)> cb) { onPage_ = std::move(cb); }

private:
    const fire::Config& config_;
    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;

    fire::CandidatesView view_;
    std::vector<RECT> candidateRects_;  // 命中测试用
    RECT menuRect_ = {0, 0, 0, 0};      // ⚙ 菜单图标命中区域
    bool visible_ = false;
    bool darkMode_ = false;             // 当前渲染使用的深色模式（每次 Show 时刷新一次）
    POINT lastPos_ = {0, 0};            // 上次窗口左上角（UpdateLayeredWindow 需要）

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

    static bool IsDarkMode();

    // 获取当前窗口所在显示器的 DPI 缩放因子（1.0 = 96 DPI）
    // 用于在高 DPI 显示器（4K/Retina）上等比放大字体与间距。
    float GetDpiScale() const;

    // 中英文提示自动隐藏定时器 ID
    enum { kToastTimerId = 1 };
};

}  // namespace firewin
