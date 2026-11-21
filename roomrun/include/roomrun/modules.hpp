// Data-driven module library with certified entry/exit state envelopes.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "roomrun/json.hpp"
#include "roomrun/params.hpp"

namespace rr {

enum class Kind { Start, Track, Ramp, Dash, Spring, Drop, Gap, Rail, Loop, Homing, SkillRail, Count };
enum class MClass { Forgiving, Mover, Free, Skill };

const char* kindName(Kind k);
const char* className(MClass c);

// State envelope at a module boundary. Speeds are *scaled* (per-frame, after speedScale).
struct Envelope {
    double vMin = 0, vMax = 99, lat = 99, head = 180;
    // Join rule: E_out(prev) must be a subset of E_in(next).
    bool subsetOf(const Envelope& o) const {
        return vMin >= o.vMin - 1e-9 && vMax <= o.vMax + 1e-9 && lat <= o.lat + 1e-9 && head <= o.head + 1e-9;
    }
};

struct ModuleDef {
    std::string id;
    Kind kind = Kind::Track;
    MClass cls = MClass::Forgiving;
    double lenMin = 16, lenMax = 200, lenStep = 8;  // units
    double maxCurv = 0.05;                          // 1/units
    double maxSlope = 0;                            // dh/ds
    double dhMin = 0, dhMax = 0;                    // units
    bool walls = false, net = false, fixedExit = false;
    Envelope entry, exit;
    double fun = 0;
    std::map<std::string, double> params;
    double param(const std::string& k, double def = 0) const {
        auto it = params.find(k);
        return it == params.end() ? def : it->second;
    }
};

struct ModuleLibrary {
    std::string name;
    std::vector<ModuleDef> defs;
    int find(const std::string& id) const;
    int findKind(Kind k) const;
    static ModuleLibrary fromJson(const Json& j, double speedScale, std::string* err = nullptr);
    static ModuleLibrary builtin(double speedScale);  // embedded copy of data/modules.json
};


// Optional "physics" block in a library file (e.g. slow Chao Race physics) overriding GenParams.
void applyPhysicsOverrides(Physics& p, const Json& libraryJson);
}  // namespace rr
