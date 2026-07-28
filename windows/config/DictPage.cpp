//
//  DictPage.cpp — 词库管理页：选择码表、调用 tablebuilder 生成 sqlite、编辑用户词库
//
#include "ConfigApp.h"
#include "DictPage.h"
#include "ConfigStore.h"

IMPLEMENT_DYNCREATE(CDictPage, CPropertyPage)

BEGIN_MESSAGE_MAP(CDictPage, CPropertyPage)
    ON_BN_CLICKED(IDC_BTN_BROWSE_WB, &CDictPage::OnBrowseWb)
    ON_BN_CLICKED(IDC_BTN_BROWSE_PY, &CDictPage::OnBrowsePy)
    ON_BN_CLICKED(IDC_BTN_BUILD_DICT, &CDictPage::OnBuildDict)
    ON_BN_CLICKED(IDC_BTN_EDIT_USERDICT, &CDictPage::OnEditUserDict)
END_MESSAGE_MAP()

CDictPage::CDictPage() : CPropertyPage(IDD_PAGE_DICT) {
    m_wbTablePath = CString(g_config.wb_table_path.c_str());
    m_pyTablePath = CString(g_config.py_table_path.c_str());
}

void CDictPage::DoDataExchange(CDataExchange* pDX) {
    CPropertyPage::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_EDIT_WB_TABLE, m_wbTablePath);
    DDX_Text(pDX, IDC_EDIT_PY_TABLE, m_pyTablePath);
}

BOOL CDictPage::OnInitDialog() {
    CPropertyPage::OnInitDialog();
    return TRUE;
}

static CString BrowseTxt(CWnd* parent) {
    CFileDialog dlg(TRUE, _T("txt"), nullptr,
                    OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    _T("码表文件 (*.txt)|*.txt|所有文件 (*.*)|*.*||"), parent);
    if (dlg.DoModal() == IDOK) return dlg.GetPathName();
    return CString();
}

void CDictPage::OnBrowseWb() {
    CString p = BrowseTxt(this);
    if (!p.IsEmpty()) { m_wbTablePath = p; UpdateData(FALSE); }
}

void CDictPage::OnBrowsePy() {
    CString p = BrowseTxt(this);
    if (!p.IsEmpty()) { m_pyTablePath = p; UpdateData(FALSE); }
}

// 等待进程结束，同时抽送消息队列，避免 UI 线程假死（对应原 WaitForSingleObject(INFINITE)）。
static DWORD WaitProcessPumping(HANDLE hProcess) {
    for (;;) {
        DWORD w = MsgWaitForMultipleObjects(1, &hProcess, FALSE, INFINITE, QS_ALLINPUT);
        if (w == WAIT_OBJECT_0) return WAIT_OBJECT_0;  // 进程结束
        if (w == WAIT_OBJECT_0 + 1) {
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            continue;
        }
        return w;  // 失败
    }
}

static bool FileExists(const CString& path) {
    if (path.IsEmpty()) return false;
    DWORD attr = GetFileAttributes(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

// 同步运行 tablebuilder 一个子命令；返回进程退出码（-1 表示无法启动）。
static int RunBuilder(const std::wstring& builder, const CString& args) {
    CString cmd;
    cmd.Format(_T("\"%s\" %s"), builder.c_str(), (LPCTSTR)args);

    STARTUPINFO si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessW(nullptr, cmd.GetBuffer(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        cmd.ReleaseBuffer();
        return -1;
    }
    cmd.ReleaseBuffer();
    WaitProcessPumping(pi.hProcess);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

void CDictPage::OnBuildDict() {
    UpdateData(TRUE);

    bool hasWb = FileExists(m_wbTablePath);
    bool hasPy = FileExists(m_pyTablePath);
    // 构建前校验码表路径存在，避免把无效路径传给 tablebuilder。
    if (!hasWb && !hasPy) {
        SetDlgItemText(IDC_STATIC_BUILD_STATUS, _T("请先选择存在的五笔或拼音码表文件"));
        return;
    }

    g_config.wb_table_path = CT2A(m_wbTablePath, CP_UTF8).m_psz;
    g_config.py_table_path = CT2A(m_pyTablePath, CP_UTF8).m_psz;

    std::wstring db = firecfg::GetDictDbPath();

    // tablebuilder.exe（与配置程序同目录）子命令协议：
    //   --create-dict <txt> <table> <db>   码表 txt 导入指定表（wb_dict / py_dict）
    //   --combine-dict <db>                合并 wb_dict + py_dict 生成内核使用的 wb_py_dict
    wchar_t exeDir[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    std::wstring dir(exeDir);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring builder = dir + L"\\tablebuilder.exe";

    // 每次构建从干净的库开始，避免 combine 重复插入历史数据。
    DeleteFileW(db.c_str());

    SetDlgItemText(IDC_STATIC_BUILD_STATUS, _T("正在构建词库…"));

    auto fail = [&](const TCHAR* msg) { SetDlgItemText(IDC_STATIC_BUILD_STATUS, msg); };

    CString args;
    if (hasWb) {
        args.Format(_T("--create-dict \"%s\" wb_dict \"%s\""), (LPCTSTR)m_wbTablePath, db.c_str());
        int code = RunBuilder(builder, args);
        if (code == -1) { fail(_T("无法启动 tablebuilder.exe")); return; }
        if (code != 0) { fail(_T("五笔码表导入失败")); return; }
    }
    if (hasPy) {
        args.Format(_T("--create-dict \"%s\" py_dict \"%s\""), (LPCTSTR)m_pyTablePath, db.c_str());
        int code = RunBuilder(builder, args);
        if (code == -1) { fail(_T("无法启动 tablebuilder.exe")); return; }
        if (code != 0) { fail(_T("拼音码表导入失败")); return; }
    }

    // 合并生成内核实际查询的 wb_py_dict 表。
    args.Format(_T("--combine-dict \"%s\""), db.c_str());
    int code = RunBuilder(builder, args);
    if (code == -1) { fail(_T("无法启动 tablebuilder.exe")); return; }
    if (code != 0) { fail(_T("词库合并失败")); return; }

    g_config.db_path = CT2A(CString(db.c_str()), CP_UTF8).m_psz;
    fail(_T("词库构建成功"));
}

void CDictPage::OnEditUserDict() {
    std::wstring path = firecfg::GetUserDictPath();
    // 确保文件存在
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    // 用记事本打开用户词库
    ShellExecuteW(m_hWnd, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
}
