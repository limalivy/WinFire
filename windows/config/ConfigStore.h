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
};

}  // namespace firecfg
