//
//  dict_service.h — 查字/统计服务抽象接口（DLL 与后台进程共用）
//
//  把 InputEngine 对 DictManager / Statistics 的直接依赖抽象成一个纯虚接口，
//  为后续「查字进程分离」铺路：
//    - DictLocalImpl：本进程直接持有 DictManager(+Statistics)，等价现状行为。
//    - DictIpcProxy （Windows 层）：把调用编码为 IPC 请求转发给 fire_dictd.exe。
//  详见 docs/dict-ipc-design.md §4.3。
//
#pragma once

#include <string>
#include <vector>

#include "fire/candidate.h"
#include "fire/dict_manager.h"  // QueryResult

namespace fire {

class IDictService {
public:
    virtual ~IDictService() = default;

    // ---- 查询（同步，热路径）----
    virtual QueryResult GetCandidates(const std::string& query, int page) = 0;
    virtual QueryResult GetReverseLookup(const std::string& pinyin, int page) = 0;

    // ---- 调频 / 用户词 ----
    virtual void RememberDynamicFrequency(const std::string& query, const Candidate& candidate) = 0;
    virtual void SetCandidateToFirst(const std::string& query, const Candidate& candidate) = 0;
    virtual bool PrependCandidate(const Candidate& candidate) = 0;
    virtual std::vector<Candidate> GetUserCandidates() = 0;
    virtual void Reinit() = 0;

    // ---- 统计（异步，fire-and-forget）----
    virtual void RecordCandidate(const Candidate& candidate,
                                 const std::string& app_id,
                                 const std::vector<std::string>& hanzi_parts,
                                 bool enable_stats,
                                 bool enable_hanzi) = 0;

    // ---- LRU 缓存落盘（异步，fire-and-forget）----
    // 通知后台尽快把内存 LRU 快照写到持久文件。Deactivate 时触发，使本次会话的
    // 缓存积累即使 daemon 后续被强杀也已落盘。不保证保存成功；后台带节流。
    // 默认空实现：本地实现（DictLocalImpl）无持久化，no-op。
    virtual void SaveCache(const std::string& app_id) { (void)app_id; }

    // ---- 状态 ----
    // 后台可用 / 本地 DB 打开成功。为 false 时引擎应降级透传。
    virtual bool IsAvailable() const = 0;

    // ---- 可用性恢复 ----
    // 当 IsAvailable()=false 时，外层可在按键热路径上周期性调用本方法尝试恢复
    //（重连 / 重新握手）。返回是否已重新可用。
    // 默认实现：本地实现（DictLocalImpl）无"恢复"概念，返回当前可用状态即可，
    // 故提供非纯虚默认实现，避免改动本地实现。
    virtual bool TryRecover() { return IsAvailable(); }

    // ---- DLL 本地缓存校验（仅 DictIpcProxy 有意义）----
    // 向后台校验本地候选缓存是否仍有效（Activate / 配置变更 / 重连后调用）。
    // 默认空实现：本地实现（DictLocalImpl）直接查库，无 DLL 层缓存概念，no-op。
    virtual void ValidateCache() {}
};

}  // namespace fire
