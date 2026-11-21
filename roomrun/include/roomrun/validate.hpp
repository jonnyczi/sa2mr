// Bot validation (many perturbed runs, three driver profiles) and the repair loop.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "roomrun/course.hpp"
#include "roomrun/sim.hpp"

namespace rr {

struct ValidationReport {
    bool completable = false;
    int runs = 0, failedRuns = 0, falls = 0;  // falls: no-net drops to the floor (checkpoint restart)
    std::vector<int> fails, expertNets, averageNets, badNets;  // per module
    std::vector<int> needsRepair;                              // module indices
    std::vector<std::string> failReasons;
    double expertLap = 0, averageLap = 0, badLap = 0;          // flying-lap seconds (mean)
    double expertRun = 0, averageRun = 0;                      // 3-lap totals (mean)
    std::vector<PathSample> expertPath;
};

ValidationReport validateCourse(const Course& c, const ModuleLibrary& lib, const GenParams& gp, uint64_t seed);

// Applies one round of repairs to modules flagged by the report. `levels` remembers how far
// each spot (keyed by start station) has been escalated. Returns false if nothing could change.
bool repairCourse(Course& c, const ValidationReport& rep, const ModuleLibrary& lib, const GenParams& gp,
                  std::map<int, int>& levels, std::vector<std::string>* log = nullptr);

}  // namespace rr
