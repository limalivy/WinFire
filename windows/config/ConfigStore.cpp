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
    // 优先读取安装器写入的注册表绝对路径（Fix B）。
    // TIP 可能被加载到 SearchHost.exe 等 SYSTEM/AppContainer 进程中，此时
    // CSIDL_APPDATA 指向系统目录而非用户目录，注册表固化路径是唯一可靠来源。
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\WinFire", 0,
                      KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        wchar_t value[1024] = {0};
        DWORD size = sizeof(value);
        DWORD type = 0;
        if (RegQueryValueExW(hKey, L"UserDataDir", nullptr, &type,
                             (BYTE*)value, &size) == ERROR_SUCCESS &&
            type == REG_SZ && value[0] != 0) {
            RegCloseKey(hKey);
            DWORD attr = GetFileAttributesW(value);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                return std::wstring(value);
            }
        } else {
            RegCloseKey(hKey);
        }
    }

    // 回退：CSIDL_APPDATA（适用于用户进程）
    wchar_t appdata[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)) || appdata[0] == 0) {
        return L".\\WinFire";
    }
    std::wstring dir = std::wstring(appdata) + L"\\WinFire";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
std::wstring GetConfigJsonPath() { return GetConfigDir() + L"\\config.json"; }
std::wstring GetUserDictPath()   { return GetConfigDir() + L"\\user-dict.txt"; }
std::wstring GetDictDbPath()     { return GetConfigDir() + L"\\wb_py_dict.sqlite"; }
std::wstring GetThemesDir()      { return GetConfigDir() + L"\\themes"; }
void EnsureThemesDir() {
    std::wstring dir = GetThemesDir();
    CreateDirectoryW(dir.c_str(), nullptr);  // 已存在时返回 ERROR_ALREADY_EXISTS，不算错误
}
std::wstring GetTablesDir() {
    // 程序 EXE 同目录下的 tables 子目录（安装版即 %ProgramFiles%\WinFire\tables）
    wchar_t exe[MAX_PATH] = {0};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return std::wstring();
    std::wstring d(exe);
    size_t p = d.find_last_of(L"\\/");
    if (p == std::wstring::npos) return std::wstring();
    return d.substr(0, p) + L"\\tables";
}

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

// ---- 主题 JSON 编解码（兼容业火 ThemeConfig.swift）----

// 单个十六进制字符转数值
int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 解析业火颜色：接受 "#RGB"/"#RGBA"/"#RRGGBB"/"#RRGGBBAA" 或
// {"red":..,"green":..,"blue":..,"opacity":..} 对象。失败返回 false。
// 与 Fire ColorData.init(hex:) 一致：nibble 形式每位置翻倍成字节；alpha 缺省 255。
bool ParseColor(const std::string& json, const std::string& key, fire::ColorData& out) {
    std::string v;
    if (!FindRaw(json, key, v)) return false;
    // 对象形式：{"red":..,"green":..,"blue":..,"opacity":..}
    if (!v.empty() && v[0] == '{') {
        fire::ColorData c;
        std::string t;
        bool any = false;
        if (FindRaw(v, "red", t)) { try { c.red = std::stod(t); any = true; } catch (...) {} }
        if (FindRaw(v, "green", t)) { try { c.green = std::stod(t); any = true; } catch (...) {} }
        if (FindRaw(v, "blue", t)) { try { c.blue = std::stod(t); any = true; } catch (...) {} }
        if (FindRaw(v, "opacity", t)) { try { c.opacity = std::stod(t); any = true; } catch (...) {} }
        if (!any) return false;
        out = c;
        return true;
    }
    // hex 字符串形式（FindRaw 对字符串值已去引号，故 v 不含前后引号）
    // 跳过前导 #
    size_t i = 0;
    if (i < v.size() && v[i] == '#') ++i;
    std::string hex = v.substr(i);
    for (char c : hex) if (HexVal(c) < 0) return false;
    auto pair2byte = [&](size_t pos) -> int {
        return (HexVal(hex[pos]) << 4) | HexVal(hex[pos + 1]);
    };
    long r = 0, g = 0, b = 0, a = 255;
    if (hex.size() == 3) {
        r = HexVal(hex[0]) * 17; g = HexVal(hex[1]) * 17; b = HexVal(hex[2]) * 17;
    } else if (hex.size() == 4) {
        r = HexVal(hex[0]) * 17; g = HexVal(hex[1]) * 17; b = HexVal(hex[2]) * 17;
        a = HexVal(hex[3]) * 17;
    } else if (hex.size() == 6) {
        r = pair2byte(0); g = pair2byte(2); b = pair2byte(4);
    } else if (hex.size() == 8) {
        r = pair2byte(0); g = pair2byte(2); b = pair2byte(4); a = pair2byte(6);
    } else {
        return false;
    }
    out.red = r / 255.0; out.green = g / 255.0; out.blue = b / 255.0; out.opacity = a / 255.0;
    return true;
}

// 输出业火 hexString：#RRGGBB，opacity<1 时追加 AA。大写、clamp、×255 round。
std::string WriteColor(const fire::ColorData& c) {
    auto to8 = [](double v) -> int {
        if (v < 0) v = 0; if (v > 1) v = 1;
        return (int)(v * 255.0 + 0.5);
    };
    char buf[16];
    if (c.opacity >= 1.0 - 1e-12) {
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", to8(c.red), to8(c.green), to8(c.blue));
    } else {
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X",
                      to8(c.red), to8(c.green), to8(c.blue), to8(c.opacity));
    }
    return buf;
}

// 定位 json 中 key 对应的对象体（不含外层 { }），返回子串。
// 用于在 theme 对象里取 "light"/"dark" 子对象交给字段解析。
bool FindObject(const std::string& json, const std::string& key, std::string& out) {
    std::string k = "\"" + key + "\"";
    size_t p = std::string::npos;
    for (size_t search = 0; (search = json.find(k, search)) != std::string::npos; search += k.size()) {
        size_t after = search + k.size();
        size_t q = after;
        while (q < json.size() && (json[q] == ' ' || json[q] == '\t' || json[q] == '\n' ||
                                   json[q] == '\r')) ++q;
        if (q < json.size() && json[q] == ':' && IsRealKeyPos(json, search)) { p = search; break; }
    }
    if (p == std::string::npos) return false;
    p = json.find('{', p + k.size());
    if (p == std::string::npos) return false;
    // 配对括号（忽略字符串内的括号——主题值均为 hex 串/数字，不含 {}）
    int depth = 0;
    size_t e = p;
    for (; e < json.size(); ++e) {
        if (json[e] == '{') ++depth;
        else if (json[e] == '}') { --depth; if (depth == 0) break; }
    }
    if (depth != 0 || e == std::string::npos) return false;
    out = json.substr(p, e - p + 1);  // 含外层 { }，供子解析复用
    return true;
}

// 取浮点字段（缺省时保持 def，与业火 decodeIfPresent 一致）
float GetFloat(const std::string& json, const std::string& key, float def) {
    std::string v; if (!FindRaw(json, key, v)) return def;
    try { return std::stof(v); } catch (...) { return def; }
}

// 把一个外观对象（json 片段，含 { }）解析填入 ap。缺字段保持 ap 现值（已由结构体默认值设置）。
void ParseAppearance(const std::string& json, fire::AppearanceThemeConfig& ap) {
    ParseColor(json, "windowBackgroundColor", ap.window_background_color);
    ap.window_padding_top = GetFloat(json, "windowPaddingTop", ap.window_padding_top);
    ap.window_padding_left = GetFloat(json, "windowPaddingLeft", ap.window_padding_left);
    ap.window_padding_right = GetFloat(json, "windowPaddingRight", ap.window_padding_right);
    ap.window_padding_bottom = GetFloat(json, "windowPaddingBottom", ap.window_padding_bottom);
    ap.window_border_radius = GetFloat(json, "windowBorderRadius", ap.window_border_radius);
    ParseColor(json, "originCodeColor", ap.origin_code_color);
    ap.origin_candidates_space = GetFloat(json, "originCandidatesSpace", ap.origin_candidates_space);
    ap.candidate_space = GetFloat(json, "candidateSpace", ap.candidate_space);
    ParseColor(json, "candidateIndexColor", ap.candidate_index_color);
    ParseColor(json, "candidateTextColor", ap.candidate_text_color);
    ParseColor(json, "candidateCodeColor", ap.candidate_code_color);
    ParseColor(json, "selectedIndexColor", ap.selected_index_color);
    ParseColor(json, "selectedTextColor", ap.selected_text_color);
    ParseColor(json, "selectedCodeColor", ap.selected_code_color);
    ParseColor(json, "pageIndicatorColor", ap.page_indicator_color);
    ParseColor(json, "pageIndicatorDisabledColor", ap.page_indicator_disabled_color);
    std::string fn;
    if (FindRaw(json, "fontName", fn)) ap.font_name = fn;
    ap.font_size = GetFloat(json, "fontSize", ap.font_size);
    // v2 字段（v1 缺省时用业火默认：indexFontSize/codeFontSize 取 font_size，enableLiquidGlass=true，
    // candidateRadius=0，candidatePadding*=2，originPadding*=0，selectedBackgroundColor 透明）
    ap.enable_liquid_glass = GetBool(json, "enableLiquidGlass", ap.enable_liquid_glass);
    ParseColor(json, "selectedBackgroundColor", ap.selected_background_color);
    ap.candidate_radius = GetFloat(json, "candidateRadius",
        GetFloat(json, "selectedBackgroundRadius", ap.candidate_radius));
    ap.candidate_padding_top = GetFloat(json, "candidatePaddingTop",
        GetFloat(json, "selectedPaddingTop", ap.candidate_padding_top));
    ap.candidate_padding_left = GetFloat(json, "candidatePaddingLeft",
        GetFloat(json, "selectedPaddingLeft", ap.candidate_padding_left));
    ap.candidate_padding_right = GetFloat(json, "candidatePaddingRight",
        GetFloat(json, "selectedPaddingRight", ap.candidate_padding_right));
    ap.candidate_padding_bottom = GetFloat(json, "candidatePaddingBottom",
        GetFloat(json, "selectedPaddingBottom", ap.candidate_padding_bottom));
    ap.origin_padding_top = GetFloat(json, "originPaddingTop", ap.origin_padding_top);
    ap.origin_padding_left = GetFloat(json, "originPaddingLeft", ap.origin_padding_left);
    ap.origin_padding_right = GetFloat(json, "originPaddingRight", ap.origin_padding_right);
    ap.origin_padding_bottom = GetFloat(json, "originPaddingBottom", ap.origin_padding_bottom);
    ap.index_font_size = GetFloat(json, "indexFontSize", ap.index_font_size);
    ap.code_font_size = GetFloat(json, "codeFontSize", ap.code_font_size);
}

// 序列化一个外观对象为 JSON 文本（缩进 base 个空格）
std::string SerializeAppearance(const fire::AppearanceThemeConfig& ap, const std::string& ind) {
    std::ostringstream o;
    o << "{\n";
    o << ind << "  \"windowBackgroundColor\": \"" << WriteColor(ap.window_background_color) << "\",\n";
    o << ind << "  \"windowPaddingTop\": " << ap.window_padding_top << ",\n";
    o << ind << "  \"windowPaddingBottom\": " << ap.window_padding_bottom << ",\n";
    o << ind << "  \"windowPaddingLeft\": " << ap.window_padding_left << ",\n";
    o << ind << "  \"windowPaddingRight\": " << ap.window_padding_right << ",\n";
    o << ind << "  \"windowBorderRadius\": " << ap.window_border_radius << ",\n";
    o << ind << "  \"enableLiquidGlass\": " << Bool(ap.enable_liquid_glass) << ",\n";
    o << ind << "  \"originCodeColor\": \"" << WriteColor(ap.origin_code_color) << "\",\n";
    o << ind << "  \"originCandidatesSpace\": " << ap.origin_candidates_space << ",\n";
    o << ind << "  \"originPaddingTop\": " << ap.origin_padding_top << ",\n";
    o << ind << "  \"originPaddingLeft\": " << ap.origin_padding_left << ",\n";
    o << ind << "  \"originPaddingRight\": " << ap.origin_padding_right << ",\n";
    o << ind << "  \"originPaddingBottom\": " << ap.origin_padding_bottom << ",\n";
    o << ind << "  \"candidateSpace\": " << ap.candidate_space << ",\n";
    o << ind << "  \"candidateIndexColor\": \"" << WriteColor(ap.candidate_index_color) << "\",\n";
    o << ind << "  \"candidateTextColor\": \"" << WriteColor(ap.candidate_text_color) << "\",\n";
    o << ind << "  \"candidateCodeColor\": \"" << WriteColor(ap.candidate_code_color) << "\",\n";
    o << ind << "  \"candidateRadius\": " << ap.candidate_radius << ",\n";
    o << ind << "  \"candidatePaddingTop\": " << ap.candidate_padding_top << ",\n";
    o << ind << "  \"candidatePaddingLeft\": " << ap.candidate_padding_left << ",\n";
    o << ind << "  \"candidatePaddingRight\": " << ap.candidate_padding_right << ",\n";
    o << ind << "  \"candidatePaddingBottom\": " << ap.candidate_padding_bottom << ",\n";
    o << ind << "  \"selectedIndexColor\": \"" << WriteColor(ap.selected_index_color) << "\",\n";
    o << ind << "  \"selectedTextColor\": \"" << WriteColor(ap.selected_text_color) << "\",\n";
    o << ind << "  \"selectedCodeColor\": \"" << WriteColor(ap.selected_code_color) << "\",\n";
    o << ind << "  \"selectedBackgroundColor\": \"" << WriteColor(ap.selected_background_color) << "\",\n";
    o << ind << "  \"pageIndicatorColor\": \"" << WriteColor(ap.page_indicator_color) << "\",\n";
    o << ind << "  \"pageIndicatorDisabledColor\": \"" << WriteColor(ap.page_indicator_disabled_color) << "\",\n";
    o << ind << "  \"fontName\": \"" << EscapeJson(ap.font_name) << "\",\n";
    o << ind << "  \"fontSize\": " << ap.font_size << ",\n";
    o << ind << "  \"indexFontSize\": " << ap.index_font_size << ",\n";
    o << ind << "  \"codeFontSize\": " << ap.code_font_size << "\n";
    o << ind << "}";
    return o.str();
}

// 解析完整主题 JSON（业火独立主题文件或 config.json 内 theme 段）。
// 校验 id/name/author 非空；v1/v2 都接受，缺 v2 字段用结构体默认值。
bool ParseThemeJsonImpl(const std::string& json, fire::ThemeConfig& out) {
    std::string id, name, author;
    if (!FindRaw(json, "id", id) || id.empty()) return false;
    if (!FindRaw(json, "name", name) || name.empty()) return false;
    if (!FindRaw(json, "author", author) || author.empty()) return false;
    out.id = id; out.name = name; out.author = author;
    out.schema_version = GetInt(json, "schemaVersion", out.schema_version);
    out.dark_mode_preference = GetInt(json, "darkModePreference", out.dark_mode_preference);
    std::string lightObj, darkObj;
    if (FindObject(json, "light", lightObj)) ParseAppearance(lightObj, out.light);
    if (FindObject(json, "dark", darkObj)) ParseAppearance(darkObj, out.dark);
    return true;
}

}  // namespace

bool ConfigStore::Load(fire::Config& c) {
    std::string json = ReadFileUtf8(GetConfigJsonPath());
    if (json.empty()) return false;
    return LoadFromString(c, json);
}

bool ConfigStore::LoadFromString(fire::Config& c, const std::string& json) {
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

    // 码表路径（词库管理页选中的五笔/拼音码表完整路径）
    // FindRaw 不做 JSON 反转义，路径含 \ 会被 EscapeJson 写成 \\ ，这里手动还原。
    {
        std::string v;
        if (FindRaw(json, "wbTablePath", v)) {
            for (size_t i = 0; i + 1 < v.size(); ++i) {
                if (v[i] == '\\' && v[i + 1] == '\\') { v.erase(i, 1); }
            }
            c.wb_table_path = v;
        }
        if (FindRaw(json, "pyTablePath", v)) {
            for (size_t i = 0; i + 1 < v.size(); ++i) {
                if (v[i] == '\\' && v[i + 1] == '\\') { v.erase(i, 1); }
            }
            c.py_table_path = v;
        }
    }

    // 主题（v1/v2 兼容）。config.json 无 theme 段时保持 c.theme 默认值。
    {
        std::string themeObj;
        if (FindObject(json, "theme", themeObj)) {
            // ParseThemeJsonImpl 要求 id/name/author 非空；内联主题必含这些字段。
            ParseThemeJsonImpl(themeObj, c.theme);
        }
    }
    return true;
}

std::string ConfigStore::Serialize(const fire::Config& c) {
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
    o << "],\n";
    o << "  \"wbTablePath\": \"" << EscapeJson(c.wb_table_path) << "\",\n";
    o << "  \"pyTablePath\": \"" << EscapeJson(c.py_table_path) << "\",\n";
    // 主题（内联完整内容，作为唯一真相源经现有 config_json IPC 下发 DLL）。
    o << "  \"theme\": {\n";
    o << "    \"schemaVersion\": " << c.theme.schema_version << ",\n";
    o << "    \"id\": \"" << EscapeJson(c.theme.id) << "\",\n";
    o << "    \"name\": \"" << EscapeJson(c.theme.name) << "\",\n";
    o << "    \"author\": \"" << EscapeJson(c.theme.author) << "\",\n";
    o << "    \"darkModePreference\": " << c.theme.dark_mode_preference << ",\n";
    o << "    \"light\": " << SerializeAppearance(c.theme.light, "    ") << ",\n";
    o << "    \"dark\": " << SerializeAppearance(c.theme.dark, "    ") << "\n";
    o << "  }\n";
    o << "}\n";
    return o.str();
}

bool ConfigStore::Save(const fire::Config& c) {
    return WriteFileUtf8(GetConfigJsonPath(), Serialize(c));
}

bool ConfigStore::SaveAtomicFromString(const std::string& json) {
    // 原子写：同目录临时文件 -> MoveFileExW(REPLACE_EXISTING)。
    // 同目录保证 rename 是原子操作（跨卷 rename 不原子）。
    std::wstring final_path = GetConfigJsonPath();
    std::wstring tmp = final_path + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(json.data(), (std::streamsize)json.size());
        if (!f) return false;
    }
    if (!MoveFileExW(tmp.c_str(), final_path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        // 替换失败（可能 config.json 被其它读者持有）→ 删临时文件，返回失败。
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// 把主题序列化为业火格式独立 JSON（不含 config.json 其它字段）。
// 供导出主题/写主题库文件使用：顶层即 schemaVersion/id/name/author/light/dark。
std::string SerializeTheme(const fire::ThemeConfig& theme) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"schemaVersion\": " << theme.schema_version << ",\n";
    o << "  \"id\": \"" << EscapeJson(theme.id) << "\",\n";
    o << "  \"name\": \"" << EscapeJson(theme.name) << "\",\n";
    o << "  \"author\": \"" << EscapeJson(theme.author) << "\",\n";
    o << "  \"light\": " << SerializeAppearance(theme.light, "  ") << ",\n";
    o << "  \"dark\": " << SerializeAppearance(theme.dark, "  ") << "\n";
    o << "}\n";
    return o.str();
}

// 解析主题 JSON 文本（独立主题文件）。校验 id/name/author 非空。
bool ParseThemeJson(const std::string& json, fire::ThemeConfig& out) {
    if (json.empty()) return false;
    // 用一份可变默认值承载缺省字段，再让 ParseThemeJsonImpl 覆盖命中字段。
    fire::ThemeConfig fresh;
    return ParseThemeJsonImpl(json, fresh) ? (out = fresh, true) : false;
}

bool LoadThemeFile(const std::wstring& path, fire::ThemeConfig& out) {
    return ParseThemeJson(ReadFileUtf8(path), out);
}

bool SaveThemeFile(const std::wstring& path, const fire::ThemeConfig& theme) {
    return WriteFileUtf8(path, SerializeTheme(theme));
}

}  // namespace firecfg
