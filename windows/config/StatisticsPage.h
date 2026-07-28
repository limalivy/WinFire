//
//  StatisticsPage.h — “输入统计”属性页
//
//  对应 macOS 版 StatisticsPane.swift：展示累计字数、不同词条数、字词频列表，
//  支持清除统计 / 仅清除字词频 / 导出 CSV，并开关统计功能。
//
#pragma once
#ifndef RC_INVOKED
#include <afxdlgs.h>
#include <afxcmn.h>
#endif

#define IDD_PAGE_STATS           2005
#define IDC_CHK_ENABLE_STATS     2501
#define IDC_CHK_ENABLE_HANZI     2502
#define IDC_STATIC_TOTAL_COUNT   2503
#define IDC_STATIC_UNIQUE_COUNT  2504
#define IDC_LIST_HANZI_FREQ      2505
#define IDC_BTN_STATS_REFRESH    2506
#define IDC_BTN_STATS_CLEAR      2507
#define IDC_BTN_STATS_CLEAR_HANZI 2508
#define IDC_BTN_STATS_EXPORT     2509

class CStatisticsPage : public CPropertyPage {
    DECLARE_DYNCREATE(CStatisticsPage)
public:
    CStatisticsPage();

    BOOL m_enableStats = FALSE;
    BOOL m_enableHanzi = FALSE;

protected:
    BOOL OnInitDialog() override;
    void DoDataExchange(CDataExchange* pDX) override;
    BOOL OnApply() override;
    afx_msg void OnRefresh();
    afx_msg void OnClear();
    afx_msg void OnClearHanzi();
    afx_msg void OnExport();
    DECLARE_MESSAGE_MAP()

private:
    void RefreshView();
    void SyncToConfig();
};
