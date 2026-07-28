//
//  candidate.cpp — CandidateType 序列化，对应 Fire/types.swift 的 rawValue
//
#include "fire/candidate.h"

namespace fire {

std::string to_string(CandidateType type) {
    switch (type) {
        case CandidateType::Wb: return "wb";
        case CandidateType::Py: return "py";
        case CandidateType::User: return "user";
        case CandidateType::Placeholder: return "placeholder";
    }
    return "wb";
}

bool candidate_type_from_string(const std::string& s, CandidateType& out) {
    if (s == "wb") { out = CandidateType::Wb; return true; }
    if (s == "py") { out = CandidateType::Py; return true; }
    if (s == "user") { out = CandidateType::User; return true; }
    if (s == "placeholder") { out = CandidateType::Placeholder; return true; }
    return false;
}

}  // namespace fire
