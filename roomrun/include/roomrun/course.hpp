// A generated course: skeleton + placed modules + baked per-station geometry.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "roomrun/modules.hpp"
#include "roomrun/params.hpp"
#include "roomrun/profile.hpp"
#include "roomrun/skeleton.hpp"

namespace rr {

struct Placed {
    int def = 0;
    Kind kind = Kind::Track;
    int s0 = 0, s1 = 0;      // station range [s0, s1)
    double h0 = 0, h1 = 0;   // entry / exit surface height (m)
    Envelope in, out;
    int lipN = 0, holeN = 0, padN = 0;  // kind-specific sub-ranges (stations)
    bool repaired = false;
    int n() const { return s1 - s0; }
};

struct StationInfo {
    bool exists = false;   // solid track surface (or rail) at this station
    bool walled = false;   // invisible walls / side rails
    bool rail = false;
    bool hole = false;     // free-flight gap over a catch net
    double z = 0;          // surface height, or flight-arc height where no surface (m)
};

// Geometry of a placed module at absolute station j (s0 <= j < s1).
StationInfo stationInfo(const Placed& m, const ModuleDef& d, int j, const GenParams& gp);

// Certified exit envelope of a placed module, given its entry envelope.
Envelope transferEnvelope(const Placed& m, const ModuleDef& d, const Envelope& in, const Skeleton& sk,
                          const GenParams& gp);

// Gap/drop sizing from the entry envelope (conservative arcs). Returns false if it doesn't fit.
bool sizeGap(const ModuleDef& d, const Envelope& in, double dhU, const GenParams& gp, int& n, int& lipN, int& holeN);
bool sizeDrop(const ModuleDef& d, const Envelope& in, double dhU, const GenParams& gp, int& n, int& lipN);

struct SkillRoute { int s0 = 0, s1 = 0; double zM = 0; };  // optional upper rail (skill route)

struct CourseStats {
    double expertLap = 0, averageLap = 0, badLap = 0;  // seconds (flying lap)
    double expertRun = 0, averageRun = 0;              // 3-lap totals from standing start
    int runs = 0, failedRuns = 0, netCatches = 0, falls = 0;  // falls = no-net drops (checkpoint restarts)
    bool completable = false;
};

struct Course {
    uint64_t seed = 0;
    std::string kind;
    bool fallback = false;
    int repairs = 0;
    Skeleton sk;
    Profile prof;
    std::vector<Placed> mods;
    // baked per station
    std::vector<double> z;
    std::vector<uint8_t> exists, walled, rail, hole;
    std::vector<int> mod;
    std::vector<Vec3> rings;  // meters
    std::vector<SkillRoute> skill;
    double rankA = 0, rankB = 0, rankC = 0;  // seconds for a 3-lap run
    CourseStats stats;
};

void bakeCourse(Course& c, const ModuleLibrary& lib, const GenParams& gp);
// Recompute envelopes along the module chain; false if any join breaks E_out(prev) ⊆ E_in(next).
bool checkJoins(Course& c, const ModuleLibrary& lib, const GenParams& gp, int* badModule = nullptr);

}  // namespace rr
