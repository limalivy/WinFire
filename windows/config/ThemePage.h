//
//  ThemePage.h — "主题"属性页（纯 Win32）
//
//  主题库来源：%APPDATA%\WinFire\themes\*.json（业火主题文件，可导入导出）。
//  当前活动主题内联在 config.json 的 theme 段（唯一真相源），经现有 IPC 下发 DLL。
//  切换主题 = 把所选主题库文件内容写入 g_config.theme（保留 dark_mode_preference）。
//
//  支持导入业火 v1/v2 主题文件；深色模式偏好（跟随系统/浅色/深色）。
//
#pragma once
#ifndef RC_INVOKED
#include "UiBase.h"
#include <vector>
#include <string>
#endif

#define IDD_PAGE_THEME            2006
#define IDC_LIST_THEMES           2401
#define IDC_BTN_THEME_USE         2402
#define IDC_BTN_THEME_IMPORT      2403
#define IDC_BTN_THEME_EXPORT      2404
#define IDC_BTN_THEME_DELETE      2405
#define IDC_CMB_DARK_MODE         2406
#define IDC_STATIC_THEME_STATUS   2407

#ifndef RC_INVOKED
// 主题库条目：文件路径 + 解析出的元信息（id/name/author）。
struct ThemeLibEntry {
    std::wstring path;     // themes\<id>.json 完整路径
    std::string id;
    std::string name;
    std::string author;
};

class CThemePage : public PageBase {
public:
    CThemePage();

    std::vector<ThemeLibEntry> m_themes;  // 主题库扫描结果
    int m_selectedLib = -1;               // ListView 当前选中项索引（-1 未选）
    int m_darkMode = 0;                   // 0=跟随系统 1=浅色 2=深色

protected:
    void OnInitDialog() override;
    bool OnApply() override;
    void OnCommand(WPARAM wParam, LPARAM lParam) override;
    void OnNotify(LPNMHDR nm, LRESULT* pResult) override;

private:
    // 扫描 themes 目录填充 m_themes 并刷新 ListView；标记当前活动主题（g_config.theme.id）。
    void RefreshThemeList();
    // 从 ListView 选中项取主题库条目；未选返回 nullptr。
    const ThemeLibEntry* SelectedTheme() const;
    // 导入业火主题文件（GetOpenFileName → 校验 → 复制到 themes → 刷新）。
    void ImportTheme();
    // 导出当前活动主题为业火 JSON 文件（GetSaveFileName）。
    void ExportTheme();
    // 删除 ListView 选中主题文件（禁止删除 default）。
    void DeleteTheme();
    // 应用 ListView 选中主题为活动主题（同步到 g_config.theme）。
    void UseTheme();
};

// 工厂：创建属性页句柄（供 PropertySheet 注册）
HPROPSHEETPAGE CreateThemePage(CThemePage& page);
#endif
