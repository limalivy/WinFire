//
//  DictPage.cpp — 词库管理页（纯 Win32）：选择码表、调用 tablebuilder 生成 sqlite、编辑用户词库
//
#include "ConfigApp.h"
#include "DictPage.h"
#include "ConfigStore.h"

#include <vector>

CDictPage::CDictPage() {
    m_wbTablePath = firecfg::Utf8ToWide(g_config.wb_table_path);
    m_pyTablePath = firecfg::Utf8ToWide(g_config.py_table_path);
}

void CDictPage::OnInitDialog() {
    firecfg::SetText(hwnd, IDC_EDIT_WB_TABLE, m_wbTablePath);
    firecfg::SetText(hwnd, IDC_EDIT_PY_TABLE, m_pyTablePath);
}

// 选择 txt 码表文件（GetOpenFileNameW 替代 CFileDialog）
static std::wstring BrowseTxt(HWND hwndOwner) {
    wchar_t szFile[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwndOwner;
    ofn.lpstrFilter = L"码表文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return std::wstring();
    return std::wstring(szFile);
}

void CDictPage::OnCommand(WPARAM wParam, LPARAM /*lParam*/) {
    int code = HIWORD(wParam);
    int id = LOWORD(wParam);
    if (code != BN_CLICKED) return;

    if (id == IDC_BTN_BROWSE_WB) {
        std::wstring p = BrowseTxt(hwnd);
        if (!p.empty()) { m_wbTablePath = p; firecfg::SetText(hwnd, IDC_EDIT_WB_TABLE, p); }
    } else if (id == IDC_BTN_BROWSE_PY) {
        std::wstring p = BrowseTxt(hwnd);
        if (!p.empty()) { m_pyTablePath = p; firecfg::SetText(hwnd, IDC_EDIT_PY_TABLE, p); }
    } else if (id == IDC_BTN_BUILD_DICT) {
        // 读取最新路径
        m_wbTablePath = firecfg::GetText(hwnd, IDC_EDIT_WB_TABLE);
        m_pyTablePath = firecfg::GetText(hwnd, IDC_EDIT_PY_TABLE);

        auto FileExists = [](const std::wstring& path) -> bool {
            if (path.empty()) return false;
            DWORD attr = GetFileAttributesW(path.c_str());
            return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
        };

        bool hasWb = FileExists(m_wbTablePath);
        bool hasPy = FileExists(m_pyTablePath);
        // 构建前校验码表路径存在，避免把无效路径传给 tablebuilder。
        if (!hasWb && !hasPy) {
            firecfg::SetText(hwnd, IDC_STATIC_BUILD_STATUS, L"请先选择存在的五笔或拼音码表文件");
            return;
        }

        g_config.wb_table_path = firecfg::WideToUtf8(m_wbTablePath);
        g_config.py_table_path = firecfg::WideToUtf8(m_pyTablePath);

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

        firecfg::SetText(hwnd, IDC_STATIC_BUILD_STATUS, L"正在构建词库…");

        auto SetStatus = [&](const wchar_t* msg) { firecfg::SetText(hwnd, IDC_STATIC_BUILD_STATUS, msg); };

        // 等待进程结束，同时抽送消息队列，避免 UI 线程假死。
        auto WaitProcessPumping = [](HANDLE hProcess) -> DWORD {
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
        };

        // 同步运行 tablebuilder 一个子命令；返回进程退出码（-1 表示无法启动）。
        auto RunBuilder = [&](const std::wstring& args) -> int {
            std::wstring cmd = L"\"" + builder + L"\" " + args;
            STARTUPINFOW si = {sizeof(si)};
            PROCESS_INFORMATION pi = {0};
            std::vector<wchar_t> buf(cmd.begin(), cmd.end());
            buf.push_back(0);
            if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                return -1;
            }
            WaitProcessPumping(pi.hProcess);
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return (int)code;
        };

        std::wstring args;
        if (hasWb) {
            args = L"--create-dict \"" + m_wbTablePath + L"\" wb_dict \"" + db + L"\"";
            int code = RunBuilder(args);
            if (code == -1) { SetStatus(L"无法启动 tablebuilder.exe"); return; }
            if (code != 0) { SetStatus(L"五笔码表导入失败"); return; }
        }
        if (hasPy) {
            args = L"--create-dict \"" + m_pyTablePath + L"\" py_dict \"" + db + L"\"";
            int code = RunBuilder(args);
            if (code == -1) { SetStatus(L"无法启动 tablebuilder.exe"); return; }
            if (code != 0) { SetStatus(L"拼音码表导入失败"); return; }
        }

        // 合并生成内核实际查询的 wb_py_dict 表。
        args = L"--combine-dict \"" + db + L"\"";
        int code = RunBuilder(args);
        if (code == -1) { SetStatus(L"无法启动 tablebuilder.exe"); return; }
        if (code != 0) { SetStatus(L"词库合并失败"); return; }

        g_config.db_path = firecfg::WideToUtf8(db);
        SetStatus(L"词库构建成功");
    } else if (id == IDC_BTN_EDIT_USERDICT) {
        std::wstring path = firecfg::GetUserDictPath();
        // 确保文件存在
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        // 用记事本打开用户词库
        ShellExecuteW(hwnd, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

HPROPSHEETPAGE CreateDictPage(CDictPage& page) {
    PROPSHEETPAGEW psp = {0};
    psp.dwSize = sizeof(psp);
    psp.dwFlags = PSP_DEFAULT;
    psp.hInstance = GetModuleHandleW(nullptr);
    psp.pszTemplate = MAKEINTRESOURCEW(IDD_PAGE_DICT);
    psp.pfnDlgProc = PageDlgProc;
    psp.lParam = (LPARAM)&page;
    return CreatePropertySheetPageW(&psp);
}
