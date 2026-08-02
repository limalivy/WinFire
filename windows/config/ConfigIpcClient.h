//
//  ConfigIpcClient.h — fire_config.exe 侧的 dictd IPC 客户端
//
//  config 收敛到 dictd 后，config.exe 不再直接读写 config.json，改经 IPC：
//    - GetConfig：打开时拉全量 config + 数据文件路径（user-dict/db 等）
//    - SetConfig：保存时委托 dictd 原子写 config.json + 热重载
//  连不上 dictd 时拉起同目录 fire_dictd.exe（用 GetModuleFileName(nullptr) 定位自身，
//  与 DLL 的 NamedPipeClient 用 GetModuleAnchor 不同——EXE 无需模块锚点）。
//
//  本客户端自包含命名管道读写，不依赖 tsf/NamedPipeClient（后者绑定 DLL 专用调试日志
//  与 GetModuleAnchor），保持 config.exe 独立。
//
#pragma once

#include <string>
#include <vector>

#include "fire/ipc/protocol.h"

namespace firecfg {

// 从 dictd 拉取全量 config + 数据文件路径。client_config_token=0 强制全量。
// 成功返回 true 并填充 resp；连不上/超时/解析失败返回 false。
bool IpcGetConfig(fire::ipc::GetConfigResponse& resp,
                  uint64_t client_config_token = 0);

// 委托 dictd 写 config.json + 热重载。reload_user_dict/reinit_dict 控制连带重载。
// 成功返回 true 并填充 resp（含新 token）；失败返回 false。
bool IpcSetConfig(const std::string& config_json, bool reload_user_dict,
                  bool reinit_dict, fire::ipc::SetConfigResponse& resp);

}  // namespace firecfg
