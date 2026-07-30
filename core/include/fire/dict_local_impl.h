//
//  dict_local_impl.h — IDictService 的本地实现
//
//  内部持有 DictManager + 可选 Statistics，把接口方法转调到它们。
//  等价于把 TextService 里 dict_ / stats_ 的直接用法收拢到一处，
//  非沙箱进程 / 后台不可用时的默认（回退）实现。
//
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fire/candidate.h"
#include "fire/config.h"
#include "fire/dict_manager.h"
#include "fire/dict_service.h"
#include "fire/statistics.h"

namespace fire {

class DictLocalImpl : public IDictService {
public:
    // dict 为词库（必需）；stats 为统计库（可选，未开启统计时传 nullptr）。
    // 两者的生命周期由本对象接管。
    DictLocalImpl(std::unique_ptr<DictManager> dict, std::unique_ptr<Statistics> stats);
    ~DictLocalImpl() override = default;

    DictLocalImpl(const DictLocalImpl&) = delete;
    DictLocalImpl& operator=(const DictLocalImpl&) = delete;

    QueryResult GetCandidates(const std::string& query, int page) override;
    QueryResult GetReverseLookup(const std::string& pinyin, int page) override;
    void RememberDynamicFrequency(const std::string& query, const Candidate& candidate) override;
    void SetCandidateToFirst(const std::string& query, const Candidate& candidate) override;
    bool PrependCandidate(const Candidate& candidate) override;
    std::vector<Candidate> GetUserCandidates() override;
    void Reinit() override;
    void RecordCandidate(const Candidate& candidate,
                         const std::string& app_id,
                         const std::vector<std::string>& hanzi_parts,
                         bool enable_stats,
                         bool enable_hanzi) override;
    bool IsAvailable() const override;

    // 直接访问底层对象（配置界面/词库管理等非引擎路径可能需要）。
    DictManager* dict() { return dict_.get(); }
    Statistics* stats() { return stats_.get(); }

private:
    std::unique_ptr<DictManager> dict_;
    std::unique_ptr<Statistics> stats_;
};

}  // namespace fire
