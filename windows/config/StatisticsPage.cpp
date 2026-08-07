//
//  StatisticsPage.cpp — 输入统计页（纯 Win32）
//
#include "ConfigApp.h"
#include "StatisticsPage.h"
#include "ConfigStore.h"

#include "fire/statistics.h"

namespace {
// 统计库路径：默认 %APPDATA%\WinFire\statistics.sqlite
std::string StatsDbPath() {
    if (!g_config.stats_db_path.empty()) return g_config.stats_db_path;
    std::wstring w = firecfg::GetConfigDir() + L"\\statistics.sqlite";
    return firecfg::WideToUtf8(w);
}
}  // namespace

CStatisticsPage::CStatisticsPage() {
    m_enableStats = g_config.enable_statistics ? TRUE : FALSE;
    m_enableHanzi = g_config.enable_hanzi_frequency_statistics ? TRUE : FALSE;
}

void CStatisticsPage::OnInitDialog() {
    firecfg::SetCheck(hwnd, IDC_CHK_ENABLE_STATS, m_enableStats);
    firecfg::SetCheck(hwnd, IDC_CHK_ENABLE_HANZI, m_enableHanzi);

    firecfg::LvInitColumns(hwnd, IDC_LIST_HANZI_FREQ);
    firecfg::LvAddColumn(hwnd, IDC_LIST_HANZI_FREQ, 0, L"词", 120, LVCFMT_LEFT);
    firecfg::LvAddColumn(hwnd, IDC_LIST_HANZI_FREQ, 1, L"次数", 80, LVCFMT_RIGHT);

    RefreshView();
}

void CStatisticsPage::RefreshView() {
    fire::Statistics stats(StatsDbPath());
    if (!stats.is_open()) {
        firecfg::SetText(hwnd, IDC_STATIC_TOTAL_COUNT, L"累计输入字数：（暂无数据）");
        firecfg::SetText(hwnd, IDC_STATIC_UNIQUE_COUNT, L"不同词条数：0");
        AutoSizeWordColumn();
        return;
    }
    wchar_t buf[128];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE,
                 L"累计输入字数：%lld", (long long)stats.query_total_count());
    firecfg::SetText(hwnd, IDC_STATIC_TOTAL_COUNT, buf);

    _snwprintf_s(buf, _countof(buf), _TRUNCATE,
                 L"不同词条数：%lld", (long long)stats.query_hanzi_frequency_unique_count());
    firecfg::SetText(hwnd, IDC_STATIC_UNIQUE_COUNT, buf);

    firecfg::LvClear(hwnd, IDC_LIST_HANZI_FREQ);
    auto rows = stats.query_hanzi_frequency(200);
    int r = 0;
    for (const auto& w : rows) {
        std::wstring word = firecfg::Utf8ToWide(w.word);
        firecfg::LvInsertItem(hwnd, IDC_LIST_HANZI_FREQ, r, word.c_str());
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%lld", (long long)w.count);
        firecfg::LvSetItem(hwnd, IDC_LIST_HANZI_FREQ, r, 1, buf);
        ++r;
    }
    AutoSizeWordColumn();
}

// 数据填充完成后调整"词"列宽度，使其填满控件客户区剩余空间（扣除"次数"列 80px）。
// 必须在数据填充后调用：此时垂直滚动条是否出现已确定，GetClientRect 反映的是真实
// 可用宽度（已扣除滚动条）。若在填充前测量，滚动条出现后可用宽度变小，两列总和会
// 超出可视区，触发水平滚动条，把"次数"列右半挤出可视范围。
void CStatisticsPage::AutoSizeWordColumn() {
    HWND lv = GetDlgItem(hwnd, IDC_LIST_HANZI_FREQ);
    if (!lv) return;
    RECT rc = {0};
    GetClientRect(lv, &rc);
    int wordWidth = (rc.right - rc.left) - 80;  // 80 为"次数"列固定宽
    if (wordWidth < 60) wordWidth = 60;          // 下限保护，避免控件过窄时挤压
    SendMessageW(lv, LVM_SETCOLUMNWIDTH, 0, (LPARAM)wordWidth);
}

void CStatisticsPage::OnCommand(WPARAM wParam, LPARAM /*lParam*/) {
    int code = HIWORD(wParam);
    int id = LOWORD(wParam);
    if (code != BN_CLICKED) return;

    if (id == IDC_BTN_STATS_REFRESH) {
        RefreshView();
    } else if (id == IDC_BTN_STATS_CLEAR) {
        if (MsgBox(L"确定清除全部输入统计数据吗？此操作不可恢复。",
                   L"确认", MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }
        fire::Statistics stats(StatsDbPath());
        if (stats.is_open()) stats.clear();
        RefreshView();
    } else if (id == IDC_BTN_STATS_CLEAR_HANZI) {
        if (MsgBox(L"确定仅清除字词频统计吗？",
                   L"确认", MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }
        fire::Statistics stats(StatsDbPath());
        if (stats.is_open()) stats.clear_hanzi_frequency();
        RefreshView();
    } else if (id == IDC_BTN_STATS_EXPORT) {
        // CSV 导出：GetSaveFileNameW 替代 CFileDialog
        wchar_t szFile[MAX_PATH] = L"hanzi_frequency.csv";
        OPENFILENAMEW ofn = {0};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0\0";
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
        ofn.lpstrDefExt = L"csv";
        if (!GetSaveFileNameW(&ofn)) return;
        std::string p = firecfg::WideToUtf8(szFile);

        fire::Statistics stats(StatsDbPath());
        bool ok = stats.is_open() && stats.export_hanzi_frequency_csv(p);
        MsgBox(ok ? L"导出成功" : L"导出失败",
               L"提示", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
    }
}

void CStatisticsPage::SyncToConfig() {
    g_config.enable_statistics = m_enableStats != FALSE;
    g_config.enable_hanzi_frequency_statistics = m_enableHanzi != FALSE;
}

bool CStatisticsPage::OnApply() {
    m_enableStats = firecfg::GetCheck(hwnd, IDC_CHK_ENABLE_STATS);
    m_enableHanzi = firecfg::GetCheck(hwnd, IDC_CHK_ENABLE_HANZI);
    SyncToConfig();
    return true;
}

HPROPSHEETPAGE CreateStatisticsPage(CStatisticsPage& page) {
    PROPSHEETPAGEW psp = {0};
    psp.dwSize = sizeof(psp);
    psp.dwFlags = PSP_DEFAULT;
    psp.hInstance = GetModuleHandleW(nullptr);
    psp.pszTemplate = MAKEINTRESOURCEW(IDD_PAGE_STATS);
    psp.pfnDlgProc = PageDlgProc;
    psp.lParam = (LPARAM)&page;
    return CreatePropertySheetPageW(&psp);
}
