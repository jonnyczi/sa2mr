#include "roomrun/validate.hpp"

#include <algorithm>
#include <cmath>

namespace rr {

ValidationReport validateCourse(const Course& c, const ModuleLibrary& lib, const GenParams& gp, uint64_t seed) {
    ValidationReport rep;
    size_t M = c.mods.size();
    rep.fails.assign(M, 0); rep.expertNets.assign(M, 0); rep.averageNets.assign(M, 0); rep.badNets.assign(M, 0);
    std::vector<int> fallsE(M, 0), fallsA(M, 0), fallsB(M, 0);
    const int laps = 3;
    int nExpert = std::max(1, gp.botRuns / 8), nAvg = std::max(1, gp.botRuns * 3 / 8);
    int nBad = std::max(1, gp.botRuns - nExpert - nAvg);
    struct Acc { double lap = 0, run = 0; int n = 0; } accE, accA, accB;
    Rng rng(seed ^ 0xB07B07ull);
    auto doRuns = [&](const BotProfile& bp, int count, std::vector<int>& nets, std::vector<int>& falls, Acc& acc, bool record) {
        for (int r = 0; r < count; ++r) {
            RunResult res = simulateRun(c, lib, gp, bp, rng.fork(), laps, record && r == 0);
            ++rep.runs;
            for (size_t m = 0; m < M; ++m) { nets[m] += res.netsPerModule[m]; falls[m] += res.fallsPerModule[m]; rep.falls += res.fallsPerModule[m]; }
            if (gp.debug && res.finished && !res.detail.empty() && rep.failReasons.size() < 6) rep.failReasons.push_back(bp.name + ":events [" + res.detail + "]");
            if (record && r == 0) rep.expertPath = std::move(res.path);
            if (!res.finished) {
                ++rep.failedRuns;
                if (res.failModule >= 0) rep.fails[res.failModule]++;
                rep.failReasons.push_back(bp.name + ":" + failName(res.fail) + "@" +
                                          (res.failModule >= 0 ? lib.defs[c.mods[res.failModule].def].id : "?") + (gp.debug ? " [" + res.detail + "]" : ""));
                continue;
            }
            double fly = 0;
            for (int l = 1; l < laps; ++l) fly += res.lapFrames[l];
            acc.lap += fly / (laps - 1) / 60.0;
            acc.run += res.totalFrames / 60.0;
            acc.n++;
        }
    };
    doRuns(BotProfile::expert(), nExpert, rep.expertNets, fallsE, accE, true);
    doRuns(BotProfile::average(), nAvg, rep.averageNets, fallsA, accA, false);
    doRuns(BotProfile::bad(), nBad, rep.badNets, fallsB, accB, false);
    auto mean = [](const Acc& a, bool run) { return a.n ? (run ? a.run : a.lap) / a.n : 0.0; };
    rep.expertLap = mean(accE, false); rep.expertRun = mean(accE, true);
    rep.averageLap = mean(accA, false); rep.averageRun = mean(accA, true);
    rep.badLap = mean(accB, false);
    rep.completable = rep.failedRuns == 0;
    for (size_t m = 0; m < M; ++m) {
        bool bad = rep.fails[m] > 0 || rep.expertNets[m] > 0 ||
                   rep.averageNets[m] > 0.10 * nAvg * laps || rep.badNets[m] > 0.5 * nBad * laps ||
                   fallsE[m] > 0 || fallsA[m] > 0 || fallsB[m] > 0.1 * nBad * laps;
        if (bad) rep.needsRepair.push_back((int)m);
    }
    return rep;
}

// Safest geometry-preserving replacement for a module (same span, same end heights).
static bool makeSafe(Placed& m, const ModuleLibrary& lib, const GenParams& gp, const Skeleton& sk) {
    double dhU = (m.h1 - m.h0) / gp.metersPerUnit();
    double lenU = m.n() * gp.stationU;
    double kmax = 0;
    for (int j = m.s0; j < m.s1; ++j) kmax = std::max(kmax, std::fabs(sk.curv[j]));
    int iTrack = lib.findKind(Kind::Track), iRail = lib.findKind(Kind::Rail), iSpring = lib.findKind(Kind::Spring);
    auto set = [&](int def) {
        if (def == m.def) return false;
        m.def = def; m.kind = lib.defs[def].kind; m.lipN = m.holeN = m.padN = 0; m.repaired = true;
        return true;
    };
    if (m.kind == Kind::Start) return false;
    if (std::fabs(dhU) < 1e-6) {
        if (m.kind == Kind::Track && kmax > lib.defs[iTrack].maxCurv) return set(iRail);  // tight turn -> rail
        if (m.kind == Kind::Rail) return false;
        return kmax <= lib.defs[iTrack].maxCurv ? set(iTrack) : set(iRail);
    }
    // A climb or descent: a rail strip of the same profile (eased stays eased, a constant slope stays constant:
    // the heights mid-module are what keep stacked decks apart), its slope walkable on the inside of its bends.
    auto walk = [&](double hwU) { return std::max(0.05, 1.0 - kmax * hwU) * 0.97; };
    const bool linear = !lib.defs[m.def].eased && (m.kind == Kind::Ramp || m.kind == Kind::Rail);
    const int iSpiral = lib.find("spiral_rail");
    const int iSame = linear && iSpiral >= 0 ? iSpiral : iRail;
    const ModuleDef& r = lib.defs[iSame];
    const double shape = r.eased ? 2 / kPi : 1.0;
    const double lim = dhU > 0 ? std::min(r.maxSlope, gp.walkRise * walk(gp.railHalfWidthU)) : std::min(r.maxFall, gp.walkFall * walk(gp.railHalfWidthU));
    if (m.def != iSame && std::fabs(dhU) <= lim * lenU * shape && kmax <= r.maxCurv && (linear || r.eased)) return set(iSame);
    if (linear) return false;   // a spring's flight would leave its bridge mid-module, under or over a stacked deck
    for (int j = m.s0; j < m.s1; ++j) if (!sk.partners[j].empty()) return false;   // no flights where decks stack
    const ModuleDef& s = lib.defs[iSpring];
    if (m.kind != Kind::Spring && dhU >= s.dhMin && dhU <= s.dhMax) {
        int pad = (int)std::ceil(s.param("pad", 8) / gp.stationU), land = (int)std::ceil(s.param("land", 16) / gp.stationU);
        const double bridge = dhU / ((m.n() - pad - land + 1) * gp.stationU);   // the play mesh's ramp under the flight
        if (m.n() - pad - land >= 3 && bridge <= gp.walkRise * walk(0.5 * gp.trackWidthU) && -bridge <= gp.walkFall * walk(0.5 * gp.trackWidthU)) {
            set(iSpring);
            m.padN = pad; m.holeN = m.n() - pad - land;
            return true;
        }
    }
    return false;
}

bool repairCourse(Course& c, const ValidationReport& rep, const ModuleLibrary& lib, const GenParams& gp,
                  std::map<int, int>& levels, std::vector<std::string>* log) {
    bool changed = false;
    std::string curId;
    auto note = [&](const Placed& m, const std::string& what) {
        if (log) log->push_back((curId.empty() ? lib.defs[m.def].id : curId) + "@" + std::to_string(m.s0) + ": " + what);
    };
    for (int mi : rep.needsRepair) {
        Placed& m = c.mods[mi];
        curId = lib.defs[m.def].id;
        int& lvl = levels[m.s0];
        const ModuleDef& d = lib.defs[m.def];
        bool did = false;
        if (m.kind == Kind::Gap && lvl == 0) {
            // widen: shorter hole, longer landing (same module length)
            int newHole = std::max(1, (int)std::floor(m.holeN * 0.6));
            if (newHole * gp.stationU >= d.param("minGap", 20) * 0.5 && newHole < m.holeN) {
                m.holeN = newHole; m.repaired = true; did = true; note(m, "shrink gap");
            }
        } else if ((m.kind == Kind::Gap || m.kind == Kind::Homing) && lvl <= 1) {
            int iSpring = lib.findKind(Kind::Spring);
            const ModuleDef& s = lib.defs[iSpring];
            double dhU = (m.h1 - m.h0) / gp.metersPerUnit();
            int pad = (int)std::ceil(s.param("pad", 8) / gp.stationU), land = (int)std::ceil(s.param("land", 16) / gp.stationU);
            double kmax = 0;
            for (int j = m.s0; j < m.s1; ++j) kmax = std::max(kmax, std::fabs(c.sk.curv[j]));
            const double f = std::max(0.05, 1.0 - kmax * 0.5 * gp.trackWidthU) * 0.97;
            const double bridge = dhU / ((m.n() - pad - land + 1) * gp.stationU);   // the play mesh's ramp under the flight: walkable
            if (dhU >= s.dhMin && dhU <= s.dhMax && m.n() - pad - land >= 3 && bridge <= gp.walkRise * f && -bridge <= gp.walkFall * f) {
                m.def = iSpring; m.kind = Kind::Spring; m.padN = pad; m.lipN = 0; m.holeN = m.n() - pad - land;
                m.repaired = true; did = true; note(m, "swap to spring");
            }
        }
        if (!did && m.kind == Kind::Rail && lvl == 0) {
            // widen: an exposed rail becomes walled track/ramp where the geometry allows it
            // (a constant-slope rail becomes a constant-slope ramp: the heights mid-module keep stacked decks apart)
            const int iRamp = !d.eased && lib.find("spiral_ramp") >= 0 ? lib.find("spiral_ramp") : lib.findKind(Kind::Ramp);
            const ModuleDef& ramp = lib.defs[iRamp];
            double dhU = (m.h1 - m.h0) / gp.metersPerUnit(), lenU = m.n() * gp.stationU, kmax = 0;
            for (int j = m.s0; j < m.s1; ++j) kmax = std::max(kmax, std::fabs(c.sk.curv[j]));
            const double f = std::max(0.05, 1.0 - kmax * 0.5 * gp.trackWidthU) * 0.97;
            const double lim = dhU > 0 ? std::min(ramp.maxSlope, gp.walkRise * f) : std::min(ramp.maxFall, gp.walkFall * f);
            int target = -1;
            if (std::fabs(dhU) < 1e-6 && kmax <= lib.defs[lib.findKind(Kind::Track)].maxCurv) target = lib.findKind(Kind::Track);
            else if (std::fabs(dhU) <= lim * lenU * (ramp.eased ? 2 / kPi : 1.0) && kmax <= ramp.maxCurv && ramp.eased == d.eased) target = iRamp;
            if (target >= 0) {
                m.def = target; m.kind = lib.defs[target].kind; m.repaired = true; did = true;
                note(m, "widen rail to walled track");
            }
        }
        if (!did && (m.kind == Kind::Track || m.kind == Kind::Ramp) && lvl <= 1 && mi > 0) {
            // add a dash panel: convert the tail of the previous flat track into a dash panel
            Placed& p = c.mods[mi - 1];
            int iDash = lib.find("dash_panel");
            int dn = iDash >= 0 ? (int)std::ceil(lib.defs[iDash].lenMin / gp.stationU) : 0;
            int tMin = (int)std::ceil(lib.defs[lib.findKind(Kind::Track)].lenMin / gp.stationU);
            if (iDash >= 0 && p.kind == Kind::Track && p.n() >= dn + tMin && lvl == 0) {
                Placed dash = p;
                dash.def = iDash; dash.kind = Kind::Dash; dash.s0 = p.s1 - dn; dash.repaired = true;
                p.s1 -= dn;
                c.mods.insert(c.mods.begin() + mi, dash);
                note(c.mods[mi + 1], "add dash panel before");
                lvl++;
                changed = true;
                break;  // indices shifted; re-validate before more edits
            }
        }
        if (!did) {
            if (makeSafe(m, lib, gp, c.sk)) { did = true; note(m, std::string("fallback to safe ") + kindName(m.kind)); }
        }
        if (did) { lvl++; changed = true; }
    }
    curId.clear();
    // keep joins valid: if a later module's entry is now violated, make it safe too.
    for (int guard = 0; guard < 64; ++guard) {
        int bad = -1;
        if (checkJoins(c, lib, gp, &bad)) break;
        if (!makeSafe(c.mods[bad], lib, gp, c.sk)) return false;
        note(c.mods[bad], "safe (join repair)");
        changed = true;
    }
    if (changed) {
        bakeCourse(c, lib, gp);
        c.repairs++;
    }
    return changed;
}

}  // namespace rr
