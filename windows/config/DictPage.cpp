//
//  DictPage.cpp — 词库管理页（纯 Win32）：从 tables 目录下拉选码表、生成 sqlite、编辑用户词库
//
#include "ConfigApp.h"
#include "DictPage.h"
#include "ConfigStore.h"
#include "ConfigIpcClient.h"

#include <vector>
#include <algorithm>
#include <string>
#include <fstream>
#include <cctype>

CDictPage::CDictPage() {
    m_wbTablePath = firecfg::Utf8ToWide(g_config.wb_table_path);
    m_pyTablePath = firecfg::Utf8ToWide(g_config.py_table_path);
}

// 扫描 tables 目录下所有 .txt 文件，返回文件名列表（不含路径，按字母序）。
// 目录不存在时返回空列表。
std::vector<std::wstring> CDictPage::ScanTablesDir() {
    std::vector<std::wstring> files;
    std::wstring dir = firecfg::GetTablesDir();
    if (dir.empty()) return files;

    std::wstring pattern = dir + L"\\*.txt";
    WIN32_FIND_DATAW fd = {0};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return files;  // 目录不存在或无 txt
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            files.emplace_back(fd.cFileName);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(files.begin(), files.end());  // 字母序，保证下拉顺序稳定
    return files;
}

// 从完整路径提取文件名（含扩展名）
static std::wstring Basename(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? path : path.substr(p + 1);
}

// 校验码表格式：每行应为 "code<TAB>text..."，首列（编码）必须为 ASCII 字母。
// 返回 true 表示格式正确；false 时 errMsg 给出中文错误描述（含首个出错行号）。
// 检查前 1000 个非空行，足够发现格式问题且不阻塞 UI。
// 背景：tablebuilder 把 columns[0] 当编码，若码表是 "字 编码"（字在前）格式，
// 会把文字写入 code 列、字母写入 text 列，导致查询返回空、空格上屏字母。
static bool ValidateCodeTableFormat(const std::wstring& path, std::wstring& errMsg) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        errMsg = L"无法打开码表文件";
        return false;
    }
    std::string line;
    int lineNo = 0;
    int checkedNonEmpty = 0;
    const int kMaxCheckLines = 1000;
    while (std::getline(ifs, line)) {
        ++lineNo;
        // 跳过空行（只含空白）
        bool allSpace = true;
        for (unsigned char c : line) {
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') { allSpace = false; break; }
        }
        if (allSpace) continue;
        if (++checkedNonEmpty > kMaxCheckLines) break;

        // 跳过前导空白，收集首列
        size_t i = 0, n = line.size();
        while (i < n && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= n) {
            errMsg = L"第 " + std::to_wstring(lineNo) + L" 行：首列为空";
            return false;
        }
        std::string firstCol;
        while (i < n && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '\n') {
            firstCol += line[i++];
        }
        // 首列必须全 ASCII 字母（a-z/A-Z）。UTF-8 下中文字符首字节 >= 0x80。
        for (unsigned char c : firstCol) {
            if (c >= 0x80 || !isalpha(c)) {
                errMsg = L"第 " + std::to_wstring(lineNo) +
                         L" 行：首列应为编码（ASCII 字母 a-z/A-Z），但发现非编码字符。"
                         L"请确认码表格式为 '编码<TAB>词条'（编码在前，非 '词条<空格>编码'）";
                return false;
            }
        }
        // 必须有第二列（词条）
        while (i < n && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= n) {
            errMsg = L"第 " + std::to_wstring(lineNo) + L" 行：缺少词条（仅有编码无对应文字）";
            return false;
        }
    }
    if (checkedNonEmpty == 0) {
        errMsg = L"码表文件为空或无有效数据行";
        return false;
    }
    return true;
}

// 填充下拉并尝试匹配 savedPath 对应文件名。
// - 目录无 txt：下拉为空，状态栏提示
// - savedPath 对应文件不在列表中（被删或路径不在 tables 下）：下拉选第一项并提示原选丢失
// - 正常匹配：选中对应项
void CDictPage::PopulateCombo(int comboId, const std::wstring& savedPath, const wchar_t* label) {
    auto files = ScanTablesDir();
    firecfg::CbReset(hwnd, comboId);

    if (files.empty()) {
        // tables 目录丢失或无码表：状态栏提示，构建按钮由 OnCommand 再校验
        std::wstring msg = std::wstring(label) + L"：未找到 tables 目录或目录下无码表文件";
        firecfg::SetText(hwnd, IDC_STATIC_BUILD_STATUS, msg.c_str());
        return;
    }

    // 填充下拉
    for (const auto& f : files) firecfg::CbAdd(hwnd, comboId, f.c_str());

    // 匹配已保存路径对应的文件名
    std::wstring savedName = Basename(savedPath);
    int sel = -1;
    if (!savedName.empty()) {
        for (int i = 0; i < (int)files.size(); ++i) {
            if (_wcsicmp(files[i].c_str(), savedName.c_str()) == 0) { sel = i; break; }
        }
    }

    if (sel >= 0) {
        firecfg::CbSetSel(hwnd, comboId, sel);
    } else if (!savedPath.empty()) {
        // 原选码表已丢失（文件被删或 tables 目录被换）：回退到第一项并提示
        firecfg::CbSetSel(hwnd, comboId, 0);
        std::wstring msg = std::wstring(label) + L"：原选码表 " + savedName +
                           L" 已丢失，已回退为 " + files[0] + L"，请确认或重新选择";
        firecfg::SetText(hwnd, IDC_STATIC_BUILD_STATUS, msg.c_str());
    } else {
        // 首次使用（无保存路径）：默认不选，让用户主动选
        firecfg::CbSetSel(hwnd, comboId, -1);
    }
}

// 取下拉选中文件名，拼成 tables 目录下的完整路径；未选返回空
std::wstring CDictPage::GetComboFullPath(int comboId) {
    int sel = firecfg::CbGetSel(hwnd, comboId);
    if (sel < 0) return std::wstring();
    wchar_t buf[MAX_PATH] = {0};
    GetDlgItemTextW(hwnd, comboId, buf, MAX_PATH);
    if (buf[0] == 0) return std::wstring();
    return firecfg::GetTablesDir() + L"\\" + std::wstring(buf);
}

void CDictPage::OnInitDialog() {
    PopulateCombo(IDC_CMB_WB_TABLE, m_wbTablePath, L"五笔码表");
    PopulateCombo(IDC_CMB_PY_TABLE, m_pyTablePath, L"拼音码表");
}

void CDictPage::OnCommand(WPARAM wParam, LPARAM /*lParam*/) {
    int code = HIWORD(wParam);
    int id = LOWORD(wParam);
    if (code != BN_CLICKED) return;

    if (id == IDC_BTN_BUILD_DICT) {
        // 从下拉取最新路径
        m_wbTablePath = GetComboFullPath(IDC_CMB_WB_TABLE);
        m_pyTablePath = GetComboFullPath(IDC_CMB_PY_TABLE);

        auto FileExists = [](const std::wstring& path) -> bool {
            if (path.empty()) return false;
            DWORD attr = GetFileAttributesW(path.c_str());
            return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
        };

        bool hasWb = FileExists(m_wbTablePath);
        bool hasPy = FileExists(m_pyTablePath);
        if (!hasWb && !hasPy) {
            firecfg::SetText(hwnd, IDC_STATIC_BUILD_STATUS,
                L"请先在下拉中选择存在的五笔或拼音码表（tables 目录下无可用文件）");
            return;
        }

        // 格式校验：码表必须为 "code<TAB>text"（编码在前），避免误用 "字 编码" 格式
        // 导致 tablebuilder 把文字当编码写入、查询返回空、空格上屏字母。
        std::wstring errMsg;
        if (hasWb && !ValidateCodeTableFormat(m_wbTablePath, errMsg)) {
            firecfg::SetText(hwnd, IDC_STATIC_BUILD_STATUS,
                (L"五笔码表格式错误：" + errMsg).c_str());
            return;
        }
        if (hasPy && !ValidateCodeTableFormat(m_pyTablePath, errMsg)) {
            firecfg::SetText(hwnd, IDC_STATIC_BUILD_STATUS,
                (L"拼音码表格式错误：" + errMsg).c_str());
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
        // db 文件刚被 tablebuilder 替换，立即委托 dictd 重新打开 sqlite 句柄
        //（reinit_dict=true → DictManager::reinit → clear_query_cache）。
        // 用当前 g_config（含新 db_path）作 config_json，使 config.json 同步落盘。
        fire::ipc::SetConfigResponse sr;
        firecfg::IpcSetConfig(firecfg::ConfigStore::Serialize(g_config),
                              /*reload_user_dict=*/false, /*reinit_dict=*/true, sr);
    } else if (id == IDC_BTN_EDIT_USERDICT) {
        std::wstring path = firecfg::GetUserDictPath();
        // 确保文件存在
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        // 用记事本打开用户词库
        ShellExecuteW(hwnd, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
        // 标记已编辑：OK 保存时据此带 reload_user_dict 通知 dictd 重读该文件。
        m_userDictEdited = true;
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
