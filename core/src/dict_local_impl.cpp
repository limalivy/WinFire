//
//  dict_local_impl.cpp — DictLocalImpl 实现
//
#include "fire/dict_local_impl.h"

namespace fire {

DictLocalImpl::DictLocalImpl(std::unique_ptr<DictManager> dict, std::unique_ptr<Statistics> stats)
    : dict_(std::move(dict)), stats_(std::move(stats)) {}

QueryResult DictLocalImpl::GetCandidates(const std::string& query, int page) {
    if (!dict_) return {};
    return dict_->get_candidates(query, page);
}

QueryResult DictLocalImpl::GetReverseLookup(const std::string& pinyin, int page) {
    if (!dict_) return {};
    return dict_->get_reverse_lookup_candidates(pinyin, page);
}

void DictLocalImpl::RememberDynamicFrequency(const std::string& query, const Candidate& candidate) {
    if (dict_) dict_->remember_dynamic_frequency(query, candidate);
}

void DictLocalImpl::SetCandidateToFirst(const std::string& query, const Candidate& candidate) {
    if (dict_) dict_->set_candidate_to_first(query, candidate);
}

bool DictLocalImpl::PrependCandidate(const Candidate& candidate) {
    if (!dict_) return false;
    return dict_->prepend_candidate(candidate);
}

std::vector<Candidate> DictLocalImpl::GetUserCandidates() {
    if (!dict_) return {};
    return dict_->get_user_candidates();
}

void DictLocalImpl::Reinit() {
    if (dict_) dict_->reinit();
}

void DictLocalImpl::RecordCandidate(const Candidate& candidate,
                                    const std::string& app_id,
                                    const std::vector<std::string>& hanzi_parts,
                                    bool enable_stats,
                                    bool enable_hanzi) {
    if (stats_ && stats_->is_open()) {
        stats_->record_candidate(candidate, app_id, hanzi_parts, enable_stats, enable_hanzi);
    }
}

bool DictLocalImpl::IsAvailable() const {
    return dict_ && dict_->is_open();
}

}  // namespace fire
