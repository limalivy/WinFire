//
//  ConfigStore.h — config.json 读写（把 fire::Config 与磁盘 JSON 互转）
//
//  说明：为避免引入第三方 JSON 库，这里提供一个极简的 JSON 序列化/反序列化，
//  仅覆盖 fire::Config 中需要持久化的字段。正式项目可替换为 nlohmann/json。
//
#pragma once

#include <string>
#include "fire/config.h"

namespace firecfg {

// 返回 %APPDATA%\WinFire 目录（不存在则创建）
std::wstring GetConfigDir();
std::wstring GetConfigJsonPath();
std::wstring GetUserDictPath();
std::wstring GetDictDbPath();
// 返回程序同目录下的 tables 子目录（码表存放位置，不创建）
std::wstring GetTablesDir();

class ConfigStore {
public:
    // 读取 config.json 到 config（文件不存在时保持默认值）
    static bool Load(fire::Config& config);
    // 写入 config 到 config.json
    static bool Save(const fire::Config& config);
    // 原子写入 JSON 文本到 config.json（先写同目录 .tmp 再 MoveFileEx 替换）。
    // 供 dictd 作为 config.json 的唯一写者（SetConfig / ReloadConfig 规范化写回）。
    // config.exe 不直接调本方法，改经 SetConfig IPC 委托 dictd。
    static bool SaveAtomicFromString(const std::string& json);
    // 把 config 序列化为 canonical JSON 文本（不写盘）。
    // 供 dictd 计算 config_token（Fnv1a64(canonical json)）及 SetConfig 校验一致性。
    static std::string Serialize(const fire::Config& config);
    // 从 JSON 文本解析填入 config（不读盘）。json 为空或解析失败时保持 config 原值。
    // 供 DLL 经 IPC 拿到 config_json 后原地更新、dictd SetConfig 解析请求体。
    static bool LoadFromString(fire::Config& config, const std::string& json);
};

}  // namespace firecfg
