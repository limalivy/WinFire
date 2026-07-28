//
//  StatisticsPage.cpp — 输入统计页
//
#include "ConfigApp.h"
#include "StatisticsPage.h"
#include "ConfigStore.h"

#include "fire/statistics.h"

IMPLEMENT_DYNCREATE(CStatisticsPage, CPropertyPage)

BEGIN_MESSAGE_MAP(CStatisticsPage, CPropertyPage)
    ON_BN_CLICKED(IDC_BTN_STATS_REFRESH, &CStatisticsPage::OnRefresh)
    ON_BN_CLICKED(IDC_BTN_STATS_CLEAR, &CStatisticsPage::OnClear)
    ON_BN_CLICKED(IDC_BTN_STATS_CLEAR_HANZI, &CStatisticsPage::OnClearHanzi)
    ON_BN_CLICKED(IDC_BTN_STATS_EXPORT, &CStatisticsPage::OnExport)
END_MESSAGE_MAP()

namespace {
// 统计库路径：默认 %APPDATA%\WinFire\statistics.sqlite
std::string StatsDbPath() {
    if (!g_config.stats_db_path.empty()) return g_config.stats_db_path;
    std::wstring w = firecfg::GetConfigDir() + L"\\statistics.sqlite";
    return std::string(CT2A(CString(w.c_str()), CP_UTF8).m_psz);
}
}  // namespace

CStatisticsPage::CStatisticsPage() : CPropertyPage(IDD_PAGE_STATS) {
    m_enableStats = g_config.enable_statistics ? TRUE : FALSE;
    m_enableHanzi = g_config.enable_hanzi_frequency_statistics ? TRUE : FALSE;
}

void CStatisticsPage::DoDataExchange(CDataExchange* pDX) {
    CPropertyPage::DoDataExchange(pDX);
    DDX_Check(pDX, IDC_CHK_ENABLE_STATS, m_enableStats);
    DDX_Check(pDX, IDC_CHK_ENABLE_HANZI, m_enableHanzi);
}

BOOL CStatisticsPage::OnInitDialog() {
    CPropertyPage::OnInitDialog();
    if (auto* lc = (CListCtrl*)GetDlgItem(IDC_LIST_HANZI_FREQ)) {
        lc->SetExtendedStyle(lc->GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        lc->InsertColumn(0, _T("词"), LVCFMT_LEFT, 120);
        lc->InsertColumn(1, _T("次数"), LVCFMT_RIGHT, 80);
    }
    RefreshView();
    return TRUE;
}

void CStatisticsPage::RefreshView() {
    fire::Statistics stats(StatsDbPath());
    if (!stats.is_open()) {
        SetDlgItemText(IDC_STATIC_TOTAL_COUNT, _T("累计输入字数：（暂无数据）"));
        SetDlgItemText(IDC_STATIC_UNIQUE_COUNT, _T("不同词条数：0"));
        return;
    }
    CString total;
    total.Format(_T("累计输入字数：%lld"), (long long)stats.query_total_count());
    SetDlgItemText(IDC_STATIC_TOTAL_COUNT, total);

    CString uniq;
    uniq.Format(_T("不同词条数：%lld"), (long long)stats.query_hanzi_frequency_unique_count());
    SetDlgItemText(IDC_STATIC_UNIQUE_COUNT, uniq);

    auto* lc = (CListCtrl*)GetDlgItem(IDC_LIST_HANZI_FREQ);
    if (lc) {
        lc->DeleteAllItems();
        auto rows = stats.query_hanzi_frequency(200);
        int r = 0;
        for (const auto& w : rows) {
            CString word(CA2T(w.word.c_str(), CP_UTF8));
            lc->InsertItem(r, word);
            CString cnt;
            cnt.Format(_T("%lld"), (long long)w.count);
            lc->SetItemText(r, 1, cnt);
            ++r;
        }
    }
}

void CStatisticsPage::OnRefresh() {
    RefreshView();
}

void CStatisticsPage::OnClear() {
    if (MessageBox(_T("确定清除全部输入统计数据吗？此操作不可恢复。"),
                   _T("确认"), MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    fire::Statistics stats(StatsDbPath());
    if (stats.is_open()) stats.clear();
    RefreshView();
}

void CStatisticsPage::OnClearHanzi() {
    if (MessageBox(_T("确定仅清除字词频统计吗？"),
                   _T("确认"), MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    fire::Statistics stats(StatsDbPath());
    if (stats.is_open()) stats.clear_hanzi_frequency();
    RefreshView();
}

void CStatisticsPage::OnExport() {
    CFileDialog dlg(FALSE, _T("csv"), _T("hanzi_frequency.csv"),
                    OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                    _T("CSV 文件 (*.csv)|*.csv|所有文件 (*.*)|*.*||"), this);
    if (dlg.DoModal() != IDOK) return;
    CString path = dlg.GetPathName();
    std::string p = CT2A(path, CP_UTF8).m_psz;

    fire::Statistics stats(StatsDbPath());
    bool ok = stats.is_open() && stats.export_hanzi_frequency_csv(p);
    MessageBox(ok ? _T("导出成功") : _T("导出失败"),
               _T("提示"), MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
}

void CStatisticsPage::SyncToConfig() {
    g_config.enable_statistics = m_enableStats != FALSE;
    g_config.enable_hanzi_frequency_statistics = m_enableHanzi != FALSE;
}

BOOL CStatisticsPage::OnApply() {
    if (!UpdateData(TRUE)) return FALSE;
    SyncToConfig();
    return CPropertyPage::OnApply();
}
