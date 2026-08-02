//
//  DictPage.h — "词库管理"属性页（纯 Win32）
//
//  码表来源：程序安装目录下的 tables\ 文件夹（不再用文件选择对话框）。
//  下拉列出 tables\*.txt，选中项持久化到 config.json，下次打开自动恢复。
//  异常处理：tables 目录丢失、已选码表被删时，下拉回退并提示用户重新选择。
//
#pragma once
#ifndef RC_INVOKED
#include "UiBase.h"
#include <vector>
#endif

#define IDD_PAGE_DICT            2003
#define IDC_CMB_WB_TABLE         2301
#define IDC_CMB_PY_TABLE         2302
#define IDC_BTN_BUILD_DICT       2305
#define IDC_BTN_EDIT_USERDICT    2306
#define IDC_STATIC_BUILD_STATUS  2307

class CDictPage : public PageBase {
public:
    CDictPage();

    // 上次保存的码表完整路径（从 g_config 读取，用于初始化下拉选中项）
    std::wstring m_wbTablePath;
    std::wstring m_pyTablePath;
    // 用户词库是否被打开编辑过（点过编辑按钮即置 true）。
    // OK 保存时据此决定是否带 reload_user_dict 通知 dictd 重读 user-dict.txt。
    bool m_userDictEdited = false;

protected:
    void OnInitDialog() override;
    void OnCommand(WPARAM wParam, LPARAM lParam) override;

private:
    // 扫描 tables 目录下所有 .txt 文件，返回文件名列表（不含路径，按字母序）
    std::vector<std::wstring> ScanTablesDir();
    // 填充下拉并尝试匹配 m_wbTablePath/m_pyTablePath 对应文件名；
    // missingHint 用于在状态栏提示原选码表已丢失。
    void PopulateCombo(int comboId, const std::wstring& savedPath, const wchar_t* label);
    // 取下拉当前选中文件名，拼成 tables 目录下的完整路径；未选返回空
    std::wstring GetComboFullPath(int comboId);
};

HPROPSHEETPAGE CreateDictPage(CDictPage& page);
