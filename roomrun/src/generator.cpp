#include "roomrun/generator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "roomrun/assembler.hpp"
#include "roomrun/profile.hpp"
#include "roomrun/skeleton.hpp"

namespace rr {

uint64_t courseSeed(const Room& q, uint64_t userSeed) {
    Hasher h;
    h.str(kGeneratorVersion);
    h.u64(userSeed);
    h.u64(roomHash(q));
    return h.value();
}

std::vector<std::string> checkClearance(const Course& c, const Room& room, const GenParams& gp) {
    std::vector<std::string> issues;
    const double hw = 0.5 * gp.trackWidthU * gp.metersPerUnit();
    int N = c.sk.size();
    for (int j = 0; j < N; ++j) {
        Vec2 t = (c.sk.pts[(j + 1) % N] - c.sk.pts[(j + N - 1) % N]).norm();
        Vec2 n = t.perp();
        for (double off : {-hw, 0.0, hw}) {
            Vec2 p = c.sk.pts[j] + n * off;
            auto bad = [&](const std::string& what) {
                if (issues.size() < 20) issues.push_back("station " + std::to_string(j) + ": " + what);
            };
            if (!pointInPolygon(room.floor, p) || distToPolygonBoundary(room.floor, p) < gp.wallMarginM * 0.5)
                bad("outside inset room");
            if ((p - room.player).len() < room.playerRadius) bad("inside player cylinder");
            for (size_t b = 0; b < room.boxes.size(); ++b) {
                const Box& bx = room.boxes[b];
                if (!bx.rect.contains(p)) continue;
                if (bx.height > gp.tallBoxM) bad("intersects tall furniture '" + bx.label + "'");
                else if (c.z[j] < bx.height - 1e-6) bad("below top of '" + bx.label + "'");
            }
        }
        if (c.z[j] < gp.minH - 1e-6 && issues.size() < 20)
            issues.push_back("station " + std::to_string(j) + ": below min height");
    }
    return issues;
}

static void placeRings(Course& c, const ValidationReport& rep, const GenParams& gp) {
    c.rings.clear();
    const double mpu = gp.metersPerUnit();
    double lastS = -1e9, lastAirS = -1e9;
    int inLine = 0;
    double lineGapUntil = 0;
    for (auto& ps : rep.expertPath) {
        double s = ps.s;
        if (ps.air) {
            if (s - lastAirS >= 14) {
                c.rings.push_back({ps.p.x * mpu, ps.p.y * mpu, (ps.p.z + 6) * mpu});
                lastAirS = s;
            }
            continue;
        }
        if (s < lineGapUntil) continue;
        if (s - lastS >= 16) {
            c.rings.push_back({ps.p.x * mpu, ps.p.y * mpu, (ps.p.z + 6) * mpu});
            lastS = s;
            if (++inLine >= 5) { inLine = 0; lineGapUntil = s + 110; }
        }
        if (c.rings.size() >= 300) break;
    }
}

static void placeSkillRoutes(Course& c, const ModuleLibrary& lib, const GenParams& gp) {
    c.skill.clear();
    int iSkill = lib.findKind(Kind::SkillRail);
    if (iSkill < 0) return;
    const ModuleDef& d = lib.defs[iSkill];
    double lift = d.param("lift", 34) * gp.metersPerUnit();
    auto plain = [](Kind k) { return k == Kind::Track || k == Kind::Ramp; };
    std::vector<SkillRoute> cands;
    size_t i = 0;
    while (i < c.mods.size()) {
        if (!plain(c.mods[i].kind)) { ++i; continue; }
        size_t k = i;
        while (k + 1 < c.mods.size() && plain(c.mods[k + 1].kind)) ++k;
        int s0 = c.mods[i].s0 + 3, s1 = c.mods[k].s1 - 3;
        double zTop = 0;
        for (int j = s0; j < s1; ++j) zTop = std::max(zTop, c.z[j]);
        bool ok = (s1 - s0) * gp.stationU >= d.lenMin && zTop + lift <= gp.maxH + 0.25;
        for (int j = s0; ok && j < s1; ++j) if (!c.sk.partners[j].empty()) ok = false;
        if (ok) cands.push_back({s0, s1, zTop + lift});
        i = k + 1;
    }
    std::stable_sort(cands.begin(), cands.end(), [](const SkillRoute& a, const SkillRoute& b) { return a.s1 - a.s0 > b.s1 - b.s0; });
    for (size_t k = 0; k < cands.size() && k < 2; ++k) c.skill.push_back(cands[k]);
}

static void finishCourse(Course& c, const ValidationReport& rep, const ModuleLibrary& lib, const GenParams& gp) {
    placeRings(c, rep, gp);
    placeSkillRoutes(c, lib, gp);
    c.stats.expertLap = rep.expertLap;
    c.stats.averageLap = rep.averageLap;
    c.stats.badLap = rep.badLap;
    c.stats.expertRun = rep.expertRun;
    c.stats.averageRun = rep.averageRun;
    c.stats.runs = rep.runs;
    c.stats.failedRuns = rep.failedRuns;
    int nets = 0;
    for (size_t m = 0; m < rep.averageNets.size(); ++m) nets += rep.expertNets[m] + rep.averageNets[m] + rep.badNets[m];
    c.stats.netCatches = nets;
    c.stats.falls = rep.falls;
    c.stats.completable = rep.completable;
    // ranks: expert bot sets A, average bot x1.3 sets C
    c.rankA = std::round(rep.expertRun * 1.05 * 10) / 10;
    c.rankC = std::round(std::max(rep.averageRun * 1.3, c.rankA * 1.2) * 10) / 10;
    c.rankB = std::round((c.rankA + c.rankC) * 0.5 * 10) / 10;
}

GenResult generateCourse(const Room& input, uint64_t userSeed, const GenParams& gp, const ModuleLibrary& lib) {
    auto t0 = std::chrono::steady_clock::now();
    GenResult res;
    std::string err;
    Room room = input;
    normalizeRoom(room);
    if (!validateRoom(room, &err)) { res.error = err; return res; }
    room = quantizeRoom(room);
    normalizeRoom(room);
    if (!validateRoom(room, &err)) { res.error = "after quantization: " + err; return res; }
    res.room = room;
    uint64_t seed = courseSeed(room, userSeed);
    Rng rng(seed);
    res.free = buildFreeSpace(room, gp);

    GenParams gpv = gp;   // the vertical shapes need constant-slope ramps (and rails) to follow their profiles
    {
        bool linRamp = false, rail = false;
        for (auto& d : lib.defs) { linRamp |= d.kind == Kind::Ramp && !d.eased; rail |= d.kind == Kind::Rail; }
        gpv.vertical = gp.vertical && linRamp && rail;
    }
    auto cands = skeletonCandidates(room, res.free, gpv, rng);
    if (gp.debug) std::fprintf(stderr, "  [t] free space + skeletons %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    double bestScore = -1e18;
    bool have = false;
    for (auto& sk : cands) {
        res.candidatesTried++;
        Rng crng(rng.fork());
        Course c;
        c.seed = seed;
        c.kind = sk.kind;
        c.sk = sk;
        c.prof = buildProfile(sk, room, gp, crng);
        double asmScore = 0;
        auto tA = std::chrono::steady_clock::now();
        bool assembled = assembleCourse(c.sk, c.prof, lib, gp, crng, {}, c.mods, &asmScore);
        if (gp.debug) std::fprintf(stderr, "  [t] assemble %.0f ms (N=%d)\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tA).count(), c.sk.size());
        if (!assembled) {
            if (gp.debug) std::fprintf(stderr, "[cand %s %.1fm] assembly failed\n", sk.kind.c_str(), sk.lengthM());
            continue;
        }
        bakeCourse(c, lib, gp);
        if (!checkJoins(c, lib, gp)) continue;  // assembler guarantees this; defensive
        std::map<int, int> levels;
        std::vector<std::string> log;
        std::map<std::string, int> flagged;
        ValidationReport rep;
        bool good = false;
        for (int round = 0; round <= gp.maxRepairRounds; ++round) {
            rep = validateCourse(c, lib, gp, seed + round);
            if (gp.debug && !rep.needsRepair.empty()) {
                std::fprintf(stderr, "  round %d flagged:", round);
                for (int mi : rep.needsRepair) std::fprintf(stderr, " %s@%d(E%d A%d B%d F%d)", lib.defs[c.mods[mi].def].id.c_str(), c.mods[mi].s0, rep.expertNets[mi], rep.averageNets[mi], rep.badNets[mi], rep.fails[mi]);
                for (auto& fr : rep.failReasons) std::fprintf(stderr, " {%s}", fr.c_str());
                std::fprintf(stderr, "\n");
            }
            for (int mi : rep.needsRepair) flagged[lib.defs[c.mods[mi].def].id]++;
            if (rep.completable && rep.needsRepair.empty()) { good = true; break; }
            if (round == gp.maxRepairRounds || !repairCourse(c, rep, lib, gp, levels, &log)) break;
        }
        for (auto& kv : flagged) res.moduleFailures[kv.first] += kv.second;
        if (gp.debug) {
            std::fprintf(stderr, "[cand %s %.1fm] score %.2f good=%d repairs=%d lap %.2fs fails:", sk.kind.c_str(), sk.lengthM(), asmScore, good, c.repairs, rep.expertLap);
            for (size_t k = 0; k < rep.failReasons.size() && k < 6; ++k) std::fprintf(stderr, " %s", rep.failReasons[k].c_str());
            for (auto& l : log) std::fprintf(stderr, " | %s", l.c_str());
            std::fprintf(stderr, "\n");
        }
        if (!good) continue;
        {
            auto iss = checkClearance(c, room, gp);
            if (!iss.empty()) {
                if (gp.debug) std::fprintf(stderr, "  clearance: %s\n", iss[0].c_str());
                continue;
            }
        }
        // the room's height used: the decks' range and the share stacked over another deck
        double zmin = 1e9, zmax = -1e9;
        int stacked = 0;
        for (int j = 0; j < c.sk.size(); ++j) {
            zmin = std::min(zmin, c.z[j]);
            zmax = std::max(zmax, c.z[j]);
            stacked += !c.sk.partners[j].empty();
        }
        // crossings, helices, coils and towers are the "wow" layouts when they do stack; seed jitter varies the
        // pick between good options
        double kindBonus = stacked > 0 && (sk.kind == "figure8" || sk.kind == "helix" || sk.kind == "coil" || sk.kind == "tower") ? 3.0 : 0.0;
        if (!gp.preferSkeleton.empty() && sk.kind == gp.preferSkeleton) kindBonus += 1000;
        const double space = 4.0 * (zmax - zmin) + 6.0 * stacked / std::max(1, c.sk.size());
        double score = asmScore + 0.4 * std::min(rep.expertLap, 25.0) - 0.5 * c.repairs + kindBonus + space + crng.range(0, 2.0);
        if (score > bestScore) {
            bestScore = score;
            res.course = c;
            res.report = rep;
            res.repairLog = log;
            have = true;
        }
    }
    if (!have) {
        Skeleton sk;
        if (!fallbackSkeleton(room, res.free, gp, sk)) {
            res.error = "not enough free floor for a loop: step closer to a wall or clear some space";
            return res;
        }
        Course c;
        c.seed = seed;
        c.kind = sk.kind;
        c.sk = sk;
        Rng frng(seed ^ 0xFA11BACCull);
        c.prof = buildProfile(sk, room, gp, frng);
        if (!assembleSafe(c.sk, c.prof, lib, gp, c.mods)) {
            res.error = "not enough free floor for a loop (only a sub-1.5 m ring fits): step closer to a wall";
            return res;
        }
        bakeCourse(c, lib, gp);
        checkJoins(c, lib, gp);
        c.fallback = true;
        res.report = validateCourse(c, lib, gp, seed);
        res.course = c;
        res.fallback = true;
    }
    finishCourse(res.course, res.report, lib, gp);
    res.repairs = res.course.repairs;
    res.ok = true;
    res.genMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

}  // namespace rr
