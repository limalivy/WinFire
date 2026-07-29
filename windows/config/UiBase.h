//
//  UiBase.h — 纯 Win32 配置界面基础设施
//
//  替代 MFC：提供
//    1) Utf8ToWide / WideToUtf8   —— UTF-8 <-> UTF-16 互转（替代 CA2W/CT2A/CA2T）
//    2) PageBase                  —— 属性页基类（封装 HWND + 消息分发 + OnInit/OnApply 钩子）
//    3) 小工具                    —— 控件读写辅助，避免重复 SendDlgItemMessage 样板
//
//  设计原则：最小化。仅抽象 5 个属性页共享的逻辑，不做多余封装。
//
#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>

namespace firecfg {

// ---- UTF-8 / UTF-16 互转（替代 MFC 的 CA2W / CT2A / CA2T 宏）----

inline std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

inline std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// ---- 控件读写小工具 ----

inline void SetCheck(HWND hDlg, int id, BOOL checked) {
    CheckDlgButton(hDlg, id, checked ? BST_CHECKED : BST_UNCHECKED);
}
inline BOOL GetCheck(HWND hDlg, int id) {
    return IsDlgButtonChecked(hDlg, id) == BST_CHECKED ? TRUE : FALSE;
}
inline void SetText(HWND hDlg, int id, const std::wstring& s) {
    SetDlgItemTextW(hDlg, id, s.c_str());
}
inline std::wstring GetText(HWND hDlg, int id) {
    wchar_t buf[1024] = {0};
    GetDlgItemTextW(hDlg, id, buf, 1024);
    return std::wstring(buf);
}
inline int GetInt(HWND hDlg, int id) {
    wchar_t buf[32] = {0};
    GetDlgItemTextW(hDlg, id, buf, 32);
    return _wtoi(buf);
}

// ComboBox 辅助
inline void CbReset(HWND hDlg, int id) {
    SendDlgItemMessageW(hDlg, id, CB_RESETCONTENT, 0, 0);
}
inline int CbAdd(HWND hDlg, int id, const wchar_t* s) {
    return (int)SendDlgItemMessageW(hDlg, id, CB_ADDSTRING, 0, (LPARAM)s);
}
inline int CbGetSel(HWND hDlg, int id) {
    return (int)SendDlgItemMessageW(hDlg, id, CB_GETCURSEL, 0, 0);
}
inline void CbSetSel(HWND hDlg, int id, int sel) {
    SendDlgItemMessageW(hDlg, id, CB_SETCURSEL, sel, 0);
}

// ListView 辅助（SysListView32）
inline void LvInitColumns(HWND hDlg, int id) {
    // 启用整行选择 + 网格线
    HWND lv = GetDlgItem(hDlg, id);
    if (!lv) return;
    DWORD ex = (DWORD)SendMessageW(lv, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0);
    SendMessageW(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, ex | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
}
inline void LvAddColumn(HWND hDlg, int id, int idx, const wchar_t* title, int width, int fmt = LVCFMT_LEFT) {
    HWND lv = GetDlgItem(hDlg, id);
    if (!lv) return;
    LVCOLUMNW col = {0};
    col.mask = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH;
    col.fmt = fmt;
    col.cx = width;
    col.pszText = (LPWSTR)title;
    SendMessageW(lv, LVM_INSERTCOLUMNW, idx, (LPARAM)&col);
}
inline void LvClear(HWND hDlg, int id) {
    SendDlgItemMessageW(hDlg, id, LVM_DELETEALLITEMS, 0, 0);
}
inline int LvInsertItem(HWND hDlg, int id, int row, const wchar_t* text) {
    LVITEMW it = {0};
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.iSubItem = 0;
    it.pszText = (LPWSTR)text;
    return (int)SendDlgItemMessageW(hDlg, id, LVM_INSERTITEMW, 0, (LPARAM)&it);
}
inline void LvSetItem(HWND hDlg, int id, int row, int sub, const wchar_t* text) {
    LVITEMW it = {0};
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.iSubItem = sub;
    it.pszText = (LPWSTR)text;
    SendDlgItemMessageW(hDlg, id, LVM_SETITEMW, 0, (LPARAM)&it);
}
inline std::wstring LvGetItemText(HWND hDlg, int id, int row, int sub) {
    HWND lv = GetDlgItem(hDlg, id);
    if (!lv) return std::wstring();
    wchar_t buf[256] = {0};
    LVITEMW it = {0};
    it.iItem = row;
    it.iSubItem = sub;
    it.mask = LVIF_TEXT;
    it.pszText = buf;
    it.cchTextMax = 256;
    SendMessageW(lv, LVM_GETITEMTEXTW, row, (LPARAM)&it);
    return std::wstring(buf);
}
inline int LvGetSelItem(HWND hDlg, int id) {
    return (int)SendDlgItemMessageW(hDlg, id, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
}

}  // namespace firecfg

//
//  PageBase —— 属性页基类
//
//  子类重写 OnInitDialog() / OnApply() / OnCommand() / OnNotify()。
//  PageDlgProc 是通用对话框过程，通过 DWLP_USER 关联到 PageBase 实例。
//
class PageBase {
public:
    HWND hwnd = nullptr;  // 本页对话框句柄（WM_INITDIALOG 时设置）

    virtual ~PageBase() = default;
    virtual void OnInitDialog() {}
    // PSN_APPLY：返回 true 允许关闭/应用，返回 false 阻止（用于校验失败）
    virtual bool OnApply() { return true; }
    virtual void OnCommand(WPARAM /*wParam*/, LPARAM /*lParam*/) {}
    virtual void OnNotify(LPNMHDR /*nm*/, LRESULT* /*pResult*/) {}

    // 便捷访问
    void Enable(int id, bool enable) {
        HWND w = GetDlgItem(hwnd, id);
        if (w) EnableWindow(w, enable ? TRUE : FALSE);
    }
    int MsgBox(const wchar_t* text, const wchar_t* caption, UINT flags) {
        return (int)MessageBoxW(hwnd, text, caption, flags);
    }
};

// 通用对话框过程（每页用 CreatePropertySheetPage 注册时传入）
INT_PTR CALLBACK PageDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
