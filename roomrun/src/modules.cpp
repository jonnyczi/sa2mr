#include "roomrun/modules.hpp"

namespace rr {

extern const char* kBuiltinModulesJson;  // generated from data/modules.json by CMake

static const char* kKindNames[] = {"start", "track", "ramp", "dash", "spring", "drop",
                                   "gap", "rail", "loop", "homing", "skillrail"};

const char* kindName(Kind k) { return kKindNames[(int)k]; }
const char* className(MClass c) {
    switch (c) {
        case MClass::Forgiving: return "forgiving";
        case MClass::Mover: return "mover";
        case MClass::Free: return "free";
        case MClass::Skill: return "skill";
    }
    return "?";
}

int ModuleLibrary::find(const std::string& id) const {
    for (size_t i = 0; i < defs.size(); ++i) if (defs[i].id == id) return (int)i;
    return -1;
}
int ModuleLibrary::findKind(Kind k) const {
    for (size_t i = 0; i < defs.size(); ++i) if (defs[i].kind == k) return (int)i;
    return -1;
}

static Envelope envOf(const Json& j, double k) {
    Envelope e;
    e.vMin = j.numOr("vMin", 0) * k;
    e.vMax = j.numOr("vMax", 99) * k;
    e.lat = j.numOr("lat", 99);
    e.head = j.numOr("head", 180);
    return e;
}

ModuleLibrary ModuleLibrary::fromJson(const Json& j, double k, std::string* err) {
    ModuleLibrary lib;
    lib.name = j.strOr("library", "unnamed");
    for (auto& m : j["modules"].arr) {
        ModuleDef d;
        d.id = m.strOr("id", "?");
        std::string kind = m.strOr("kind", "track");
        bool found = false;
        for (int i = 0; i < (int)Kind::Count; ++i)
            if (kind == kKindNames[i]) { d.kind = (Kind)i; found = true; }
        if (!found) { if (err) *err = "unknown module kind '" + kind + "'"; continue; }
        std::string cls = m.strOr("class", "forgiving");
        d.cls = cls == "mover" ? MClass::Mover : cls == "free" ? MClass::Free : cls == "skill" ? MClass::Skill : MClass::Forgiving;
        const Json& len = m["len"];
        if (len.isArray() && len.size() >= 2) {
            d.lenMin = len[0].num; d.lenMax = len[1].num;
            d.lenStep = len.size() > 2 ? len[2].num : 8;
        }
        d.maxCurv = m.numOr("maxCurv", 0.05);
        d.maxSlope = m.numOr("maxSlope", 0);
        const Json& dh = m["dh"];
        if (dh.isArray() && dh.size() == 2) { d.dhMin = dh[0].num; d.dhMax = dh[1].num; }
        d.walls = m.boolOr("walls", false);
        d.net = m.boolOr("net", false);
        d.fun = m.numOr("fun", 0);
        d.entry = envOf(m["entry"], k);
        if (m["exit"].isObject()) { d.exit = envOf(m["exit"], k); d.fixedExit = true; }
        for (auto& kv : m["params"].obj) d.params[kv.first] = kv.second.num;
        d.eased = d.param("ease", 1) > 0;
        d.maxFall = d.param("maxFall", d.maxSlope);
        d.minDh = d.param("minDh", 6);
        // speed-valued params are scaled too
        for (const char* sp : {"speed", "exitSpeed", "minSpeed", "speedCap"})
            if (d.params.count(sp)) d.params[sp] *= k;
        lib.defs.push_back(d);
    }
    return lib;
}

ModuleLibrary ModuleLibrary::builtin(double k) {
    return fromJson(Json::parse(kBuiltinModulesJson), k);
}


void applyPhysicsOverrides(Physics& p, const Json& j) {
    const Json& o = j["physics"];
    if (!o.isObject()) return;
    p.runAccel = o.numOr("runAccel", p.runAccel);
    p.runMax = o.numOr("runMax", p.runMax);
    p.topSpeed = o.numOr("topSpeed", p.topSpeed);
    p.dragAbove = o.numOr("dragAbove", p.dragAbove);
    p.coastDecel = o.numOr("coastDecel", p.coastDecel);
    p.brakeDecel = o.numOr("brakeDecel", p.brakeDecel);
    p.gravity = o.numOr("gravity", p.gravity);
    p.jumpV = o.numOr("jumpV", p.jumpV);
    p.grip = o.numOr("grip", p.grip);
}
}  // namespace rr
