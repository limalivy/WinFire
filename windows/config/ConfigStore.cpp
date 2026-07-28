//
//  ConfigStore.cpp — 极简 config.json 序列化
//
#include "ConfigStore.h"

#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace firecfg {

std::wstring GetConfigDir() {
    wchar_t appdata[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)) || appdata[0] == 0) {
        // 回退到当前目录，避免拼出以空字符串开头的非法路径。
        return L".\\WinFire";
    }
    std::wstring dir = std::wstring(appdata) + L"\\WinFire";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
std::wstring GetConfigJsonPath() { return GetConfigDir() + L"\\config.json"; }
std::wstring GetUserDictPath()   { return GetConfigDir() + L"\\user-dict.txt"; }
std::wstring GetDictDbPath()     { return GetConfigDir() + L"\\wb_py_dict.sqlite"; }

// ---- 极简 JSON 工具（够用即可，字段固定）----
namespace {

std::string ReadFileUtf8(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

bool WriteFileUtf8(const std::wstring& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(content.data(), (std::streamsize)content.size());
    return true;
}

// 判断位置 pos 处的 "key" 是否为真正的对象键：其前一个非空白字符必须是 '{' 或 ','。
// 避免键名误命中某个字符串值内部（如某应用进程名恰好等于配置键名）。
bool IsRealKeyPos(const std::string& json, size_t pos) {
    if (pos == 0) return false;
    size_t i = pos;
    while (i > 0) {
        char c = json[i - 1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { --i; continue; }
        return c == '{' || c == ',';
    }
    return false;
}

// 在 json 里找 "key": <value>，返回原始 token（数字/true/false/带引号字符串）
bool FindRaw(const std::string& json, const std::string& key, std::string& out) {
    std::string k = "\"" + key + "\"";
    size_t p = std::string::npos;
    for (size_t search = 0; (search = json.find(k, search)) != std::string::npos; search += k.size()) {
        // 键后必须紧跟（可含空白）冒号，且键位置是真正的对象键位置
        size_t after = search + k.size();
        size_t q = after;
        while (q < json.size() && (json[q] == ' ' || json[q] == '\t' || json[q] == '\n' ||
                                   json[q] == '\r')) ++q;
        if (q < json.size() && json[q] == ':' && IsRealKeyPos(json, search)) {
            p = search;
            break;
        }
    }
    if (p == std::string::npos) return false;
    p = json.find(':', p + k.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' ||
                               json[p] == '\r')) ++p;
    if (p >= json.size()) return false;
    if (json[p] == '"') {
        size_t e = json.find('"', p + 1);
        if (e == std::string::npos) return false;
        out = json.substr(p + 1, e - p - 1);
        return true;
    }
    size_t e = p;
    while (e < json.size() && json[e] != ',' && json[e] != '}' && json[e] != '\n') ++e;
    out = json.substr(p, e - p);
    // trim
    while (!out.empty() && (out.back() == ' ' || out.back() == '\r')) out.pop_back();
    return true;
}

bool GetBool(const std::string& json, const std::string& key, bool def) {
    std::string v; if (!FindRaw(json, key, v)) return def; return v == "true";
}
int GetInt(const std::string& json, const std::string& key, int def) {
    std::string v; if (!FindRaw(json, key, v)) return def;
    try { return std::stoi(v); } catch (...) { return def; }
}
// 读取枚举整数并做范围校验：越界（含负值/损坏数据）时回退默认值，防止把非法枚举传给内核。
int GetEnum(const std::string& json, const std::string& key, int def, int lo, int hi) {
    std::string v; if (!FindRaw(json, key, v)) return def;
    int r;
    try { r = std::stoi(v); } catch (...) { return def; }
    if (r < lo || r > hi) return def;
    return r;
}

std::string Bool(bool b) { return b ? "true" : "false"; }

// 转义 JSON 字符串：处理引号、反斜杠与常见控制字符，避免写出非法 JSON。
std::string EscapeJson(const std::string& s) {
    std::string r;
    for (unsigned char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    r += buf;
                } else {
                    r.push_back((char)c);
                }
        }
    }
    return r;
}

// 解析 "appSettings": [ {"app":"x","mode":0}, ... ] 到 map。
void ParseAppSettings(const std::string& json,
                      std::map<std::string, fire::InputModeSetting>& out) {
    std::string k = "\"appSettings\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return;
    p = json.find('[', p);
    if (p == std::string::npos) return;
    size_t end = json.find(']', p);
    if (end == std::string::npos) return;
    std::string arr = json.substr(p + 1, end - p - 1);

    size_t obj = 0;
    while ((obj = arr.find('{', obj)) != std::string::npos) {
        size_t oe = arr.find('}', obj);
        if (oe == std::string::npos) break;
        std::string item = arr.substr(obj, oe - obj + 1);
        std::string app;
        if (FindRaw(item, "app", app) && !app.empty()) {
            int mode = GetEnum(item, "mode", 0, 0, 2);
            out[app] = (fire::InputModeSetting)mode;
        }
        obj = oe + 1;
    }
}

// 解析 "customPunctuation": [ {"k":"，","v":"，"}, ... ] 到 map。
void ParseCustomPunctuation(const std::string& json,
                            std::unordered_map<std::string, std::string>& out) {
    std::string k = "\"customPunctuation\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return;
    p = json.find('[', p);
    if (p == std::string::npos) return;
    size_t end = json.find(']', p);
    if (end == std::string::npos) return;
    std::string arr = json.substr(p + 1, end - p - 1);

    size_t obj = 0;
    while ((obj = arr.find('{', obj)) != std::string::npos) {
        size_t oe = arr.find('}', obj);
        if (oe == std::string::npos) break;
        std::string item = arr.substr(obj, oe - obj + 1);
        std::string key, val;
        if (FindRaw(item, "k", key) && !key.empty() && FindRaw(item, "v", val)) {
            out[key] = val;
        }
        obj = oe + 1;
    }
}

}  // namespace

bool ConfigStore::Load(fire::Config& c) {
    std::string json = ReadFileUtf8(GetConfigJsonPath());
    if (json.empty()) return false;

    c.z_key_query = GetBool(json, "zKeyQuery", c.z_key_query);
    c.show_code_in_window = GetBool(json, "showCodeInWindow", c.show_code_in_window);
    c.wubi_code_tip = GetBool(json, "wubiCodeTip", c.wubi_code_tip);
    c.wubi_auto_commit = GetBool(json, "wubiAutoCommit", c.wubi_auto_commit);
    c.enable_word_input = GetBool(json, "enableWordInput", c.enable_word_input);
    c.enable_dynamic_frequency = GetBool(json, "enableDynamicFrequency", c.enable_dynamic_frequency);
    c.candidate_count = GetInt(json, "candidateCount", c.candidate_count);
    if (c.candidate_count < 1) c.candidate_count = 1;
    if (c.candidate_count > 10) c.candidate_count = 10;
    c.disable_en_mode = GetBool(json, "disableEnMode", c.disable_en_mode);
    c.disable_temp_en_mode = GetBool(json, "disableTempEnMode", c.disable_temp_en_mode);
    c.enable_dot_after_number = GetBool(json, "enableDotAfterNumber", c.enable_dot_after_number);
    c.enable_punctuation_commit = GetBool(json, "enablePunctuationCommit", c.enable_punctuation_commit);
    c.enable_whitespace_between_zh_en =
        GetBool(json, "enableWhitespaceBetweenZhEn", c.enable_whitespace_between_zh_en);
    c.wubi35_ding = GetBool(json, "wubi35Ding", c.wubi35_ding);

    c.code_mode = (fire::CodeMode)GetEnum(json, "codeMode", (int)c.code_mode, 0, 2);
    c.candidates_direction =
        (fire::CandidatesDirection)GetEnum(json, "candidatesDirection", (int)c.candidates_direction,
                                           0, 2);
    c.wubi_ding_mode = (fire::WubiDingMode)GetEnum(json, "wubiDingMode", (int)c.wubi_ding_mode, 0, 3);
    c.punctuation_mode = (fire::PunctuationMode)GetEnum(json, "punctuationMode", (int)c.punctuation_mode, 0, 2);
    c.toggle_input_mode_key =
        (fire::ModifierKey)GetEnum(json, "toggleInputModeKey", (int)c.toggle_input_mode_key, 0, 6);

    // 按应用输入模式
    c.keep_app_input_mode = GetBool(json, "keepAppInputMode", c.keep_app_input_mode);
    c.app_input_mode_tip_show_time =
        (fire::AppInputModeTipShowTime)GetEnum(json, "appInputModeTipShowTime",
                                               (int)c.app_input_mode_tip_show_time, 0, 2);
    c.app_settings.clear();
    ParseAppSettings(json, c.app_settings);

    // 输入统计
    c.enable_statistics = GetBool(json, "enableStatistics", c.enable_statistics);
    c.enable_hanzi_frequency_statistics =
        GetBool(json, "enableHanziFrequencyStatistics", c.enable_hanzi_frequency_statistics);

    // 自定义标点映射（仅在 punctuationMode=Custom 时使用）
    c.custom_punctuation_settings.clear();
    ParseCustomPunctuation(json, c.custom_punctuation_settings);
    return true;
}

bool ConfigStore::Save(const fire::Config& c) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"zKeyQuery\": " << Bool(c.z_key_query) << ",\n";
    o << "  \"showCodeInWindow\": " << Bool(c.show_code_in_window) << ",\n";
    o << "  \"wubiCodeTip\": " << Bool(c.wubi_code_tip) << ",\n";
    o << "  \"wubiAutoCommit\": " << Bool(c.wubi_auto_commit) << ",\n";
    o << "  \"enableWordInput\": " << Bool(c.enable_word_input) << ",\n";
    o << "  \"enableDynamicFrequency\": " << Bool(c.enable_dynamic_frequency) << ",\n";
    o << "  \"candidateCount\": " << c.candidate_count << ",\n";
    o << "  \"disableEnMode\": " << Bool(c.disable_en_mode) << ",\n";
    o << "  \"disableTempEnMode\": " << Bool(c.disable_temp_en_mode) << ",\n";
    o << "  \"enableDotAfterNumber\": " << Bool(c.enable_dot_after_number) << ",\n";
    o << "  \"enablePunctuationCommit\": " << Bool(c.enable_punctuation_commit) << ",\n";
    o << "  \"enableWhitespaceBetweenZhEn\": " << Bool(c.enable_whitespace_between_zh_en) << ",\n";
    o << "  \"wubi35Ding\": " << Bool(c.wubi35_ding) << ",\n";
    o << "  \"codeMode\": " << (int)c.code_mode << ",\n";
    o << "  \"candidatesDirection\": " << (int)c.candidates_direction << ",\n";
    o << "  \"wubiDingMode\": " << (int)c.wubi_ding_mode << ",\n";
    o << "  \"punctuationMode\": " << (int)c.punctuation_mode << ",\n";
    o << "  \"toggleInputModeKey\": " << (int)c.toggle_input_mode_key << ",\n";
    o << "  \"keepAppInputMode\": " << Bool(c.keep_app_input_mode) << ",\n";
    o << "  \"appInputModeTipShowTime\": " << (int)c.app_input_mode_tip_show_time << ",\n";
    o << "  \"enableStatistics\": " << Bool(c.enable_statistics) << ",\n";
    o << "  \"enableHanziFrequencyStatistics\": " << Bool(c.enable_hanzi_frequency_statistics) << ",\n";
    o << "  \"appSettings\": [";
    bool first = true;
    for (const auto& kv : c.app_settings) {
        if (!first) o << ", ";
        first = false;
        o << "{\"app\": \"" << EscapeJson(kv.first) << "\", \"mode\": " << (int)kv.second << "}";
    }
    o << "],\n";
    o << "  \"customPunctuation\": [";
    first = true;
    for (const auto& kv : c.custom_punctuation_settings) {
        if (!first) o << ", ";
        first = false;
        o << "{\"k\": \"" << EscapeJson(kv.first) << "\", \"v\": \"" << EscapeJson(kv.second) << "\"}";
    }
    o << "]\n";
    o << "}\n";
    return WriteFileUtf8(GetConfigJsonPath(), o.str());
}

}  // namespace firecfg
