# 查字进程分离与配置收敛（ipc/ + windows/dictd/）

> 本文是 WinFire「查字进程分离（Dict IPC）」与「配置收敛到 dictd」两个核心基础设施的设计参考文档。
> 按需阅读——日常任务只需遵循 [AGENTS.md](../AGENTS.md) 的红线规则即可；
> 涉及 IPC 协议、命名管道、AppContainer 适配、config.json 唯一真相源时再查本文。

## 1. 查字进程分离（Dict IPC，AppContainer 适配）

- **动机**：TSF DLL 会被加载进 SearchHost.exe / UWP 等 AppContainer 沙箱进程，沙箱无权
  读写 `%APPDATA%` 下词库/统计库，导致「开始菜单搜索」等场景无候选。方案：把查库/统计
  下沉到一个正常完整性级别（IL）的后台进程 `fire_dictd.exe`，DLL 端经 IPC 转发。
- **接口抽象**：`IDictService`（`core/include/fire/dict_service.h`）纯虚接口，`InputEngine`
  只依赖它。两种实现：
  - `DictLocalImpl`（内核内，直接持 `DictManager`+`Statistics`）——仅供 `fire_dictd.exe` 后台
    进程与内核测试使用；**DLL 不再链接此实现**。
  - `DictIpcProxy`（`windows/tsf/`）——DLL 唯一使用的实现，把调用编码成 IPC 请求转发给
    `fire_dictd.exe`。后台不可用时 `IsAvailable()=false`，引擎降级透传（不再回退本进程查库）。
- **协议**（`ipc/`，纯 C++17 入 CMake 可测）：16 字节定长帧头（magic `0x57464452`"WFDR" /
  version u16=1 / msg_type u16 / request_id u32 / payload_len u32）+ 手写小端二进制 payload
  （`Writer/Reader`，越界检查）。MsgType：`0x01` Hello、`0x02` QueryCandidates、`0x03`
  ReverseLookup、`0x04` RememberFreq、`0x05` SetCandidateFirst、`0x06` PrependCandidate、
  `0x07` GetUserCandidates、`0x08` RecordStat、`0x09` Reinit、`0x0A` SaveCache、
  `0x0B` CacheValidate、`0xFF` Error。
- **传输**：命名管道 `\\.\pipe\WinFire_Dict_<会话id>`（`PIPE_TYPE_MESSAGE`），SDDL
  `D:(A;;GRGW;;;WD)(A;;GRGW;;;AC)S:(ML;;NW;;;LW)` 放开 AppContainer + 低 IL 访问。
  server/client 均用 `FILE_FLAG_OVERLAPPED` 创建，所有读写走 overlapped + 超时。
  查询同步（20ms 超时，超时/失败即返回空结果并标记重连）；统计/调频/Reinit 异步
  fire-and-forget。
- **后台生命周期**：单实例 mutex `Global\WinFire_Dictd_<会话id>`；**常驻不退出**（原空闲
  超时退出已移除，保证系统进程拉起场景下后台始终可用）；**开机自启动**——安装时写入
  `HKCU\Run`（Inno 安装包，登录用户）或 `HKLM\Run`（install.ps1，全用户），值名
  `WinFireDictd`，系统重启后随用户登录自启，使 SearchHost.exe 等 AppContainer 沙箱进程
  首次输入即可经 IPC 查库出候选，不依赖沙箱进程无权的 CreateProcess。DLL 端 `DictIpcProxy`
  首次连不上仍会尝试 `CreateProcessW` 拉起同目录 `fire_dictd.exe`（非沙箱场景兜底）；
  dictd 意外死亡后 DLL 靠按键驱动的惰性自愈（`TryRecover`，1s 退避）重新拉起并适应。连接
  处理线程异常隔离（try/catch），避免单连接异常 `std::terminate` 整个后台进程。
- **数据路径与权限**：安装时 `icacls "{app}" /grant *S-1-15-2-1:(OI)(CI)(RX) /T`（授予
  ALL APPLICATION PACKAGES 读/执行）；后台以正常 IL 读写用户数据库，绕开沙箱写限制。
- **DLL 本地候选缓存（CacheValidate 协议）**：DLL 端 `DictIpcProxy` 维护一个 DLL 层本地 LRU
  缓存（`dll_cache_lru_`/`dll_cache_map_`，cap=1000），`GetCandidates` 热路径先命中本地缓存
  则 0 次 IPC 往返直接返回。缓存有效性由 `CacheValidate`（`0x0B`）IPC 请求校验：dictd 实时
  stat db 文件 mtime/size + ConfigDigest + `user_cache_generation`，FNV-1a64 压成单 u64 token；
  DLL 仅比较 token 相等性（不解析内部，算法可演进）。`allow_dll_cache` 由 dictd 裁决
 （开启动态调频时禁止，因候选顺序会变），DLL 不自行读 config 判断，避免沙箱进程读不到配置
  或多实例状态不一致。缓存失效时机覆盖：Activate 握手后、配置热加载后、重连后；通信失败时
  保守禁用并清空，不冒风险。

## 2. 配置收敛到 dictd（config.json 唯一真相源，零轮询）

- **动机**：原 DLL 每 60s `stat` config.json 发现变更、dictd 启动时一次性读后**永不重载**，
  导致多宿主 DLL 视图不一、dictd 永远停在启动快照。方案：config.json 的**读写唯一在 dictd
  进程内**发生，DLL 与 config.exe 经 IPC 获取/更新 config，**消除所有定时 stat 轮询**。
- **config_token**：`Fnv1a64(canonical config json)`，dictd 缓存 json+token。仅在 SetConfig /
  ReloadConfig / Init 时刷新（不 stat config.json）。token 与 json 内容强一致（同 json 必同 token）。
- **协议扩展**（向后兼容，只追加字段）：
  - `CacheValidate`（`0x0B`）请求加 `client_config_token`；响应加 `config_token` + 条件
    `config_json`（token 不一致时填全量，一致则空省传输）+ 4 个数据文件路径（db/stats/user_dict/cache_bin）。
  - 新增 `GetConfig`（`0x0C`，同步）：config.exe 打开时拉全量 config + 路径。
  - 新增 `SetConfig`（`0x0D`，同步 100ms）：config.exe 保存时委托 dictd 原子写 config.json
    + 原地 reload + 刷新 token；`reload_user_dict`/`reinit_dict` 控制连带重载用户词库/重建 sqlite。
  - 新增 `ReloadConfig`（`0x0E`，异步 fire-and-forget）：`fire_dictd.exe --reload-config` 命令行
    / install.ps1 触发，dictd 从磁盘重读 config.json + 规范化写回 + reinit。
- **dictd 热重载**：SetConfig handler 持锁做「原子写（temp+rename）→ LoadFromString → RefreshConfigToken
  → 可选 reload_user_dict/reinit」；ReloadConfig handler 从磁盘 Load + 规范化写回 + RefreshToken + reinit。
  **不增加任何 mtime stat**。
- **DLL（零磁盘 IO 热加载）**：`MaybeReloadConfig` 删除全部 `GetFileAttributesExW` 逻辑，保留
  60s 节流（语义从「省 stat」变「省 IPC」），到期直接 `ValidateCache(client_config_token)`。
  响应非空 config_json → `ConfigStore::LoadFromString` 原地填 `config_`（引擎经引用即见，不重建）。
  Activate 时 `LoadConfigFromDisk` 一次 bootstrap 兜底（沙箱读不到则降级 default，首次 IPC 补）。
- **config.exe（经 IPC，不碰 config.json）**：打开走 `IpcGetConfig`（拉 dictd 全量）；保存走
  `IpcSetConfig`（委托原子写+reload）；DictPage 重建词库后立即 `IpcSetConfig(reinit_dict=true)`，
  编辑 user-dict 后 OK 时带 `reload_user_dict=true`。连不上 dictd 时 `LaunchBackend`（用
  `GetModuleFileName(nullptr)` 定位自身目录，与 DLL 的 `GetModuleAnchor` 不同）+ 轮询连接；
  IPC 全失败则降级 `ConfigStore::Load/Save` 直读写（兜底，保证 config.exe 独立可用）。
- **已知限制**：非 config.exe / 非 SetConfig 改 config.json 不实时生效（需 `fire_dictd.exe
  --reload-config` 或重启 dictd）——这是零轮询的必然代价。
