//
//  ThemePage.cpp — 主题管理页（纯 Win32）：列出/选择/导入/导出/删除主题，深色模式偏好
//
#include "ConfigApp.h"
#include "ThemePage.h"
#include "ConfigStore.h"

#include <commdlg.h>
#include <algorithm>
#include <string>
#include <fstream>
#include <sstream>

CThemePage::CThemePage() {
    m_darkMode = g_config.theme.dark_mode_preference;
    if (m_darkMode < 0 || m_darkMode > 2) m_darkMode = 0;
}

// 扫描 themes 目录下所有 .json，解析元信息（id/name/author），跳过解析失败的文件。
void CThemePage::RefreshThemeList() {
    m_themes.clear();
    std::wstring dir = firecfg::GetThemesDir();
    if (!dir.empty()) {
        std::wstring pattern = dir + L"\\*.json";
        WIN32_FIND_DATAW fd = {0};
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                std::wstring full = dir + L"\\" + fd.cFileName;
                fire::ThemeConfig t;
                if (firecfg::LoadThemeFile(full, t) && !t.id.empty()) {
                    m_themes.push_back({full, t.id, t.name, t.author});
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    // 按名称排序，保证列表顺序稳定
    std::sort(m_themes.begin(), m_themes.end(),
              [](const ThemeLibEntry& a, const ThemeLibEntry& b) { return a.name < b.name; });

    // 刷新 ListView
    if (!hwnd) return;  // 构造期调用时 hwnd 尚未就绪，仅填充 m_themes
    firecfg::LvClear(hwnd, IDC_LIST_THEMES);
    const std::string& activeId = g_config.theme.id;
    int selectRow = -1;
    for (int i = 0; i < (int)m_themes.size(); ++i) {
        const auto& e = m_themes[i];
        // 活动主题在名称前加 ★ 标记
        std::wstring prefix = (e.id == activeId) ? L"\u2605 " : L"  ";
        std::wstring row = prefix + firecfg::Utf8ToWide(e.name);
        firecfg::LvInsertItem(hwnd, IDC_LIST_THEMES, i, row.c_str());
        firecfg::LvSetItem(hwnd, IDC_LIST_THEMES, i, 1, firecfg::Utf8ToWide(e.author).c_str());
        std::wstring idLine = L"ID: " + firecfg::Utf8ToWide(e.id);
        firecfg::LvSetItem(hwnd, IDC_LIST_THEMES, i, 2, idLine.c_str());
        if (e.id == activeId) selectRow = i;
    }
    m_selectedLib = selectRow;
    if (selectRow >= 0) {
        // 选中活动主题行
        HWND lv = GetDlgItem(hwnd, IDC_LIST_THEMES);
        if (lv) {
            ListView_SetItemState(lv, selectRow, LVIS_SELECTED, LVIS_SELECTED);
            ListView_EnsureVisible(lv, selectRow, FALSE);
        }
    }
}

const ThemeLibEntry* CThemePage::SelectedTheme() const {
    if (m_selectedLib < 0 || m_selectedLib >= (int)m_themes.size()) return nullptr;
    return &m_themes[m_selectedLib];
}

void CThemePage::OnInitDialog() {
    // ListView 三列：主题名 / 作者 / ID
    firecfg::LvInitColumns(hwnd, IDC_LIST_THEMES);
    firecfg::LvAddColumn(hwnd, IDC_LIST_THEMES, 0, L"主题名", 150);
    firecfg::LvAddColumn(hwnd, IDC_LIST_THEMES, 1, L"作者", 90);
    firecfg::LvAddColumn(hwnd, IDC_LIST_THEMES, 2, L"标识", 120);

    // 深色模式下拉
    firecfg::CbReset(hwnd, IDC_CMB_DARK_MODE);
    firecfg::CbAdd(hwnd, IDC_CMB_DARK_MODE, L"跟随系统");
    firecfg::CbAdd(hwnd, IDC_CMB_DARK_MODE, L"始终浅色");
    firecfg::CbAdd(hwnd, IDC_CMB_DARK_MODE, L"始终深色");
    firecfg::CbSetSel(hwnd, IDC_CMB_DARK_MODE, m_darkMode);

    // 确保 themes 目录存在（首次使用时），扫描并填充列表
    firecfg::EnsureThemesDir();
    RefreshThemeList();
    if (m_themes.empty()) {
        // 主题库为空：把当前活动主题（默认或已加载）写一份到 themes 目录，避免列表全空
        if (!g_config.theme.id.empty()) {
            std::wstring fn = firecfg::GetThemesDir() + L"\\" +
                              firecfg::Utf8ToWide(g_config.theme.id) + L".json";
            firecfg::SaveThemeFile(fn, g_config.theme);
            RefreshThemeList();
        }
    }
    if (m_themes.empty()) {
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS,
            L"主题库为空。点「导入」加载业火主题文件（.json）。");
    } else {
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, L"");
    }
}

void CThemePage::OnCommand(WPARAM wParam, LPARAM lParam) {
    int code = HIWORD(wParam);
    int id = LOWORD(wParam);
    if (code != BN_CLICKED && code != CBN_SELCHANGE) return;

    if (id == IDC_BTN_THEME_USE) {
        UseTheme();
    } else if (id == IDC_BTN_THEME_IMPORT) {
        ImportTheme();
    } else if (id == IDC_BTN_THEME_EXPORT) {
        ExportTheme();
    } else if (id == IDC_BTN_THEME_DELETE) {
        DeleteTheme();
    } else if (id == IDC_CMB_DARK_MODE && code == CBN_SELCHANGE) {
        m_darkMode = firecfg::CbGetSel(hwnd, IDC_CMB_DARK_MODE);
    }
}

void CThemePage::OnNotify(LPNMHDR nm, LRESULT* pResult) {
    if (nm->idFrom == IDC_LIST_THEMES && nm->code == LVN_ITEMCHANGED) {
        auto pnmlv = reinterpret_cast<LPNMLISTVIEW>(nm);
        if (pnmlv->uNewState & LVIS_SELECTED) {
            m_selectedLib = pnmlv->iItem;
        }
    }
}

// 应用选中主题：读文件 → 覆盖 g_config.theme（保留 dark_mode_preference，用下拉当前值）
void CThemePage::UseTheme() {
    const ThemeLibEntry* e = SelectedTheme();
    if (!e) {
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, L"请先在列表中选择一个主题。");
        return;
    }
    fire::ThemeConfig t;
    if (!firecfg::LoadThemeFile(e->path, t)) {
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, L"主题文件解析失败，无法应用。");
        return;
    }
    t.dark_mode_preference = m_darkMode;  // 保留当前深色偏好
    g_config.theme = t;
    RefreshThemeList();
    std::wstring msg = L"已应用主题：" + firecfg::Utf8ToWide(t.name);
    firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, msg.c_str());
}

void CThemePage::ImportTheme() {
    wchar_t buf[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"主题文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"json";
    if (!GetOpenFileNameW(&ofn)) return;

    // 读取并校验
    fire::ThemeConfig t;
    std::string json;
    {
        std::ifstream f(buf, std::ios::binary);
        if (!f) { firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, L"无法读取所选文件。"); return; }
        std::stringstream ss; ss << f.rdbuf(); json = ss.str();
    }
    if (!firecfg::ParseThemeJson(json, t)) {
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS,
            L"导入失败：不是有效的业火主题文件（缺少 id/name/author 或格式错误）。");
        return;
    }
    // 禁止导入与默认主题同名的主题：删除保护以 id=="default" 判定，
    // 若导入主题占用该 id（或同名“默认”），会变成无法删除的幽灵主题。
    if (t.id == "default" || t.name == "\u9ed8\u8ba4") {
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS,
            L"导入失败：主题名称或标识与默认主题冲突，请修改后再导入。");
        return;
    }
    // 复制到 themes 目录，文件名用业火导出约定 <name>-<id>-<author>.json
    firecfg::EnsureThemesDir();
    std::wstring fn = firecfg::Utf8ToWide(t.name) + L"-" +
                      firecfg::Utf8ToWide(t.id) + L"-" +
                      firecfg::Utf8ToWide(t.author) + L".json";
    // 清理文件名中可能的非法字符
    for (auto& c : fn) {
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
            c == L'"' || c == L'<' || c == L'>' || c == L'|') c = L'_';
    }
    std::wstring dst = firecfg::GetThemesDir() + L"\\" + fn;
    if (!firecfg::SaveThemeFile(dst, t)) {
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, L"写入主题库失败（目录权限？）。");
        return;
    }
    RefreshThemeList();
    std::wstring msg = L"已导入主题：" + firecfg::Utf8ToWide(t.name);
    firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, msg.c_str());
}

void CThemePage::ExportTheme() {
    // 导出当前活动主题
    if (g_config.theme.id.empty()) {
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, L"当前无活动主题可导出。");
        return;
    }
    // 默认主题不可导出：避免导出的文件被重新导入后与默认主题同名，导致无法删除。
    if (g_config.theme.id == "default") {
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, L"默认主题不可导出。");
        return;
    }
    std::wstring defaultFn = firecfg::Utf8ToWide(g_config.theme.name) + L"-" +
                             firecfg::Utf8ToWide(g_config.theme.id) + L"-" +
                             firecfg::Utf8ToWide(g_config.theme.author) + L".json";
    wchar_t buf[MAX_PATH] = {0};
    wcsncpy(buf, defaultFn.c_str(), MAX_PATH - 1);
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"主题文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"json";
    if (!GetSaveFileNameW(&ofn)) return;

    if (firecfg::SaveThemeFile(std::wstring(buf), g_config.theme)) {
        std::wstring msg = L"已导出主题到：" + std::wstring(buf);
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, msg.c_str());
    } else {
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, L"导出失败（写入权限？）。");
    }
}

void CThemePage::DeleteTheme() {
    const ThemeLibEntry* e = SelectedTheme();
    if (!e) {
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, L"请先在列表中选择要删除的主题。");
        return;
    }
    if (e->id == "default") {
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, L"默认主题不可删除。");
        return;
    }
    std::wstring msg = L"确认删除主题「" + firecfg::Utf8ToWide(e->name) + L"」？";
    if (MsgBox(msg.c_str(), L"删除主题", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (DeleteFileW(e->path.c_str())) {
        RefreshThemeList();
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, L"已删除主题。");
    } else {
        firecfg::SetText(hwnd, IDC_STATIC_THEME_STATUS, L"删除失败（文件可能被占用）。");
    }
}

bool CThemePage::OnApply() {
    // 深色偏好即时生效（即使未切换主题）
    m_darkMode = firecfg::CbGetSel(hwnd, IDC_CMB_DARK_MODE);
    if (m_darkMode < 0 || m_darkMode > 2) m_darkMode = 0;
    g_config.theme.dark_mode_preference = m_darkMode;
    return true;
}

HPROPSHEETPAGE CreateThemePage(CThemePage& page) {
    PROPSHEETPAGEW psp = {0};
    psp.dwSize = sizeof(psp);
    psp.dwFlags = PSP_DEFAULT;
    psp.hInstance = GetModuleHandleW(nullptr);
    psp.pszTemplate = MAKEINTRESOURCEW(IDD_PAGE_THEME);
    psp.pfnDlgProc = PageDlgProc;
    psp.lParam = (LPARAM)&page;
    return CreatePropertySheetPageW(&psp);
}
