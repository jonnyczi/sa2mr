// Top-level pipeline: room -> free space -> skeletons -> profile -> assembly -> bot validation
// -> repair -> (fallback) -> rings, skill routes, rank thresholds.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "roomrun/course.hpp"
#include "roomrun/field.hpp"
#include "roomrun/modules.hpp"
#include "roomrun/room.hpp"
#include "roomrun/validate.hpp"

namespace rr {

struct GenResult {
    bool ok = false;
    std::string error;
    Room room;          // quantized room the course was fitted to
    FreeSpace free;
    Course course;
    ValidationReport report;
    bool fallback = false;
    int candidatesTried = 0;
    int repairs = 0;
    std::map<std::string, int> moduleFailures;  // module id -> times flagged by the validator
    std::vector<std::string> repairLog;
    double genMs = 0;
};

uint64_t courseSeed(const Room& quantized, uint64_t userSeed);

GenResult generateCourse(const Room& room, uint64_t seed, const GenParams& gp, const ModuleLibrary& lib);

// Hard geometric checks: inside room, outside player cylinder, furniture only touched from above.
std::vector<std::string> checkClearance(const Course& c, const Room& room, const GenParams& gp);

}  // namespace rr
