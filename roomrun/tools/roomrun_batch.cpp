// roomrun-batch: generate courses for many random rooms and report robustness statistics.
//   roomrun-batch --rooms 1000 [--seed S] [--threads T] [--csv out.csv]
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "roomrun/generator.hpp"
#include "roomrun/objects.hpp"
#include "roomrun/playmesh.hpp"

using namespace rr;

struct Row {
    int room = 0;
    RoomType type = RoomType::Rectangle;
    double scaleCm = 10;
    int nBoxes = 0;
    bool ok = false, fallback = false, completable = false, clearanceOk = false;
    std::string kind, error;
    double lengthM = 0, expertLap = 0, avgLap = 0, badLap = 0, ms = 0;
    int repairs = 0, modules = 0, nets = 0, falls = 0, rejectedPlayerSpots = 0;
    std::map<std::string, int> moduleFailures, moduleUse;
    // the playable mesh the game gets (buildPlayMesh): collision triangles and pieces, the steepest walkable deck
    // facet (degrees; drops left out), the largest slope change between stations (drops left out)
    int pmTris = 0, pmPieces = 0;
    double pmSteep = 0, pmCrease = 0;
    // the game objects on it (placeObjects): counts, and the flights the checks left out
    int obRings = 0, obSprings = 0, obDash = 0, obRamps = 0, obRails = 0, obSpringsOut = 0, obRampsOut = 0, obRailsOut = 0, obSpringMods = 0;
};

static double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t i = (size_t)std::clamp(p * (v.size() - 1) + 0.5, 0.0, (double)v.size() - 1);
    return v[i];
}

int main(int argc, char** argv) {
    int rooms = 1000, threads = (int)std::max(1u, std::thread::hardware_concurrency());
    uint64_t seed = 12345;
    std::string csv, dumpDir;
    int variety = 0;   // --variety N: the game objects' mix on N courses for each of the VR player's play-area sizes
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--rooms") rooms = std::atoi(next().c_str());
        else if (a == "--seed") seed = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--threads") threads = std::atoi(next().c_str());
        else if (a == "--csv") csv = next();
        else if (a == "--dump-fallbacks") dumpDir = next();
        else if (a == "--variety") variety = std::atoi(next().c_str());
        else { std::fprintf(stderr, "usage: roomrun-batch --rooms N [--seed S] [--threads T] [--csv f]\n"); return 2; }
    }
    if (variety > 0) {
        // The VR player's rooms: a play area under 2 x 2 m becomes a 2 x 2 m course about its centre with the user
        // inside the play area (0.3 m kept free below 2.5 m, else 0.7), Sonic 12 cm (host/xr README "Room Run v0").
        struct Area { const char* name; double w, d, uw, ud; };
        const Area areas[] = {{"1 x 1 m", 2.0, 2.0, 0.4, 0.4}, {"2 x 2 m", 2.0, 2.0, 0.8, 0.8}, {"3 x 2.5 m", 3.0, 2.5, 0.9, 0.7}};
        std::printf("Room Run game objects: the share of courses with each, %d seeds and user spots a play area (Sonic 12 cm)\n", variety);
        for (const Area& a : areas) {
            GenParams gp;
            gp.sonicHeightM = 0.12;
            const ModuleLibrary lib = ModuleLibrary::builtin(gp.speedScale);
            std::atomic<int> next{0};
            std::mutex mu;
            int ok = 0, spring = 0, hop = 0, dash = 0, ramp = 0, enemy = 0, rail = 0, ring = 0, all = 0;
            double grind = 0;
            auto work = [&]() {
                for (int i; (i = next.fetch_add(1)) < variety;) {
                    Rng rng(seed * 7919ull + (uint64_t)i);
                    Room room;
                    room.floor = {{-a.w / 2, -a.d / 2}, {a.w / 2, -a.d / 2}, {a.w / 2, a.d / 2}, {-a.w / 2, a.d / 2}};
                    room.player = {rng.range(-a.uw, a.uw), rng.range(-a.ud, a.ud)};
                    room.playerRadius = a.w < 2.5 || a.d < 2.5 ? 0.3 : 0.7;
                    const GenResult r = generateCourse(room, (uint64_t)i + 1, gp, lib);
                    if (!r.ok) continue;
                    const PlayMesh pm = buildPlayMesh(r, lib, gp);
                    const CourseObjects co = placeObjects(r, pm, lib, gp);
                    const ObjectStats& s = co.stats;
                    std::lock_guard<std::mutex> g(mu);
                    ok++; spring += s.springs > 0; hop += s.hopSprings > 0; dash += s.dashPanels > 0; ramp += s.dashRamps > 0;
                    enemy += s.enemies > 0; rail += s.rails > 0; ring += s.rings > 0;
                    all += s.springs > 0 && s.dashPanels > 0 && s.enemies > 0 && s.rails > 0 && s.rings > 0;
                    grind += (double)s.grindStations / pm.st.size();
                }
            };
            std::vector<std::thread> th;
            for (int t = 0; t < threads; ++t) th.emplace_back(work);
            for (auto& t : th) t.join();
            const double k = 100.0 / std::max(1, ok);
            std::printf("%-9s courses %d of %d: springs %.0f%% (a hop spring added in %.0f%%), dash panels %.0f%%, dash ramps %.0f%%, enemies %.0f%%, "
                        "grind rails %.0f%% (%.0f%% of the course under one on average), rings %.0f%%; all of springs, panels, enemies, rails and "
                        "rings %.0f%%\n", a.name, ok, variety, spring * k, hop * k, dash * k, ramp * k, enemy * k, rail * k, grind * k, ring * k, all * k);
        }
        return 0;
    }
    const double scales[] = {0.10, 0.15};
    std::vector<Row> rows((size_t)rooms * 2);
    std::atomic<int> nextRoom{0};
    std::mutex printMu;
    auto worker = [&]() {
        while (true) {
            int i = nextRoom.fetch_add(1);
            if (i >= rooms) break;
            RoomType type = (RoomType)(i % 4);
            for (int si = 0; si < 2; ++si) {
                GenParams gp;
                gp.sonicHeightM = scales[si];
                ModuleLibrary lib = ModuleLibrary::builtin(gp.speedScale);
                Row& row = rows[(size_t)i * 2 + si];
                row.room = i; row.type = type; row.scaleCm = scales[si] * 100;
                // Sample rooms until the player spot leaves room for at least the minimal loop.
                Rng rng(seed * 1000003ull + (uint64_t)i);
                GenResult r;
                for (int attempt = 0; attempt < 50; ++attempt) {
                    Room room = randomRoom(rng, type);
                    row.nBoxes = (int)room.boxes.size();
                    r = generateCourse(room, seed + (uint64_t)i, gp, lib);
                    if (!r.ok && !dumpDir.empty()) writeFile(dumpDir + "/error_" + std::to_string(i) + "_" + std::to_string((int)(scales[si] * 100)) + ".json", roomToJson(room).dump(3, true));
                    if (r.ok || r.error.rfind("not enough free floor", 0) != 0) break;
                    row.rejectedPlayerSpots++;
                }
                row.ok = r.ok;
                row.error = r.error;
                if (r.ok) {
                    const Course& c = r.course;
                    row.fallback = c.fallback;
                    row.completable = c.stats.completable;
                    row.clearanceOk = checkClearance(c, r.room, gp).empty();
                    row.kind = c.kind;
                    row.lengthM = c.sk.lengthM();
                    row.expertLap = c.stats.expertLap;
                    row.avgLap = c.stats.averageLap;
                    row.badLap = c.stats.badLap;
                    row.repairs = c.repairs;
                    row.modules = (int)c.mods.size();
                    row.nets = c.stats.netCatches;
                    row.falls = c.stats.falls;
                    row.ms = r.genMs;
                    row.moduleFailures = r.moduleFailures;
                    if (!dumpDir.empty() && c.fallback)
                        writeFile(dumpDir + "/fallback_" + std::to_string(i) + "_" + std::to_string((int)row.scaleCm) + ".json",
                                  roomToJson(r.room).dump(3, true));
                    for (auto& m : c.mods) row.moduleUse[lib.defs[m.def].id]++;
                    {
                        const PlayMeshParams pp;
                        const PlayMesh pm = buildPlayMesh(r, lib, gp, pp);
                        row.pmTris = (int)pm.collisionTriangles();
                        row.pmPieces = (int)pm.chunks.size();
                        const CourseObjects co = placeObjects(r, pm, lib, gp);
                        row.obRings = co.stats.rings; row.obSprings = co.stats.springs; row.obDash = co.stats.dashPanels;
                        row.obRamps = co.stats.dashRamps; row.obRails = co.stats.rails; row.obSpringsOut = co.stats.springsDropped;
                        row.obRampsOut = co.stats.rampsDropped; row.obRailsOut = co.stats.railsDropped;
                        for (auto& m : c.mods) row.obSpringMods += m.kind == Kind::Spring;
                        const int N = (int)pm.st.size();
                        const double step = c.sk.stepM;
                        for (int j = 0; j < N; ++j) {
                            const double s0 = (pm.st[j].p.y - pm.st[(j + N - 1) % N].p.y) / step, s1 = (pm.st[(j + 1) % N].p.y - pm.st[j].p.y) / step;
                            if (s0 < -pp.dropSlope * 0.5 || s1 < -pp.dropSlope * 0.5) continue;
                            row.pmCrease = std::max(row.pmCrease, std::fabs(s1 - s0));
                            if (std::fabs(s1 - s0) > 0.15 && std::getenv("RR_PM_DEBUG"))
                                std::fprintf(stderr, "room %d scale %.0f: crease %.3f at station %d of %d (slopes %.3f %.3f), held %d, bridged %d, kind %s, generator %s z %.3f -> %.3f\n",
                                             i, scales[si] * 100, s1 - s0, j, N, s0, s1, pm.st[j].held, (int)pm.st[j].bridged, playKindName(pm.st[j].kind),
                                             c.exists[j] ? (c.hole[j] ? "hole" : "deck") : "none", c.z[j], pm.st[j].p.y);
                        }
                        for (auto& ch : pm.chunks) {
                            if (ch.walls) continue;
                            for (size_t t = 0; t < ch.tri.size() / 9; ++t) {
                                const float* v = &ch.tri[9 * t];
                                const double e1[3] = {v[3] - v[0], v[4] - v[1], v[5] - v[2]}, e2[3] = {v[6] - v[0], v[7] - v[1], v[8] - v[2]};
                                const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                                const double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                                if (l < 1e-12 || n[1] <= 0) continue;   // the deck's facets (up), not its underside
                                const double tilt = std::acos(std::min(1.0, n[1] / l)) * 57.29578;
                                if (tilt < std::atan(pp.dropSlope * 0.5) * 57.29578) row.pmSteep = std::max(row.pmSteep, tilt);
                            }
                        }
                    }
                }
            }
            if (i % 100 == 99) {
                std::lock_guard<std::mutex> lk(printMu);
                std::fprintf(stderr, "  %d/%d rooms\n", i + 1, rooms);
            }
        }
    };
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    std::printf("Room Run batch: %d random rooms x 2 Sonic scales (seed %llu)\n", rooms, (unsigned long long)seed);
    int totalRejected = 0;
    for (auto& r : rows) totalRejected += r.rejectedPlayerSpots;
    std::printf("player spots rejected up front (no free disk for even a minimal loop; app would ask the user to step aside): %d\n\n",
                totalRejected);
    for (int si = 0; si < 2; ++si) {
        int n = 0, ok = 0, fb = 0, comp = 0, clear = 0, rep = 0, repCourses = 0, fallCourses = 0, netCourses = 0;
        long nets = 0, falls = 0;
        std::vector<double> len, el, al, bl, ms, pmT, pmP, pmS, pmC;
        std::vector<double> obR;
        long obS = 0, obSO = 0, obD = 0, obRp = 0, obRpO = 0, obRl = 0, obRlO = 0, obSM = 0; int noRings = 0;
        std::map<std::string, int> kinds, failures, use;
        std::map<std::string, std::pair<int, int>> byType;  // ok-without-fallback / total
        for (auto& r : rows) {
            if (r.scaleCm != scales[si] * 100) continue;
            ++n;
            auto& bt = byType[roomTypeName(r.type)];
            bt.second++;
            if (!r.ok) { std::printf("  room %d (%s): error %s\n", r.room, roomTypeName(r.type), r.error.c_str()); continue; }
            ++ok;
            if (r.fallback) ++fb; else bt.first++;
            if (r.completable) ++comp;
            if (r.clearanceOk) ++clear;
            rep += r.repairs;
            if (r.repairs) ++repCourses;
            if (r.falls) ++fallCourses;
            if (r.nets) ++netCourses;
            nets += r.nets; falls += r.falls;
            obR.push_back(r.obRings); noRings += r.obRings == 0; obS += r.obSprings; obSO += r.obSpringsOut; obSM += r.obSpringMods;
            obD += r.obDash; obRp += r.obRamps; obRpO += r.obRampsOut; obRl += r.obRails; obRlO += r.obRailsOut;
            pmT.push_back(r.pmTris); pmP.push_back(r.pmPieces); pmS.push_back(r.pmSteep); pmC.push_back(r.pmCrease);
            len.push_back(r.lengthM); el.push_back(r.expertLap); al.push_back(r.avgLap); bl.push_back(r.badLap); ms.push_back(r.ms);
            kinds[r.kind]++;
            for (auto& kv : r.moduleFailures) failures[kv.first] += kv.second;
            for (auto& kv : r.moduleUse) use[kv.first] += kv.second;
        }
        std::printf("=== Sonic %.0f cm (1 unit = %.1f cm), speed scale 0.6 ===\n", scales[si] * 100, scales[si] * 10);
        std::printf("generated: %d/%d   without fallback: %.1f%%   critical path completable (all 32 bot runs): %.1f%%   clearance ok: %.1f%%\n",
                    ok, n, 100.0 * (ok - fb) / std::max(1, n), 100.0 * comp / std::max(1, ok), 100.0 * clear / std::max(1, ok));
        std::printf("no-fallback by room type:");
        for (auto& kv : byType) std::printf("  %s %.1f%%", kv.first.c_str(), 100.0 * kv.second.first / std::max(1, kv.second.second));
        std::printf("\nskeletons:");
        for (auto& kv : kinds) std::printf("  %s %d", kv.first.c_str(), kv.second);
        std::printf("\nlap length m   p5 %.1f  p50 %.1f  p95 %.1f\n", pct(len, 0.05), pct(len, 0.5), pct(len, 0.95));
        std::printf("expert lap s   p5 %.1f  p50 %.1f  p95 %.1f\n", pct(el, 0.05), pct(el, 0.5), pct(el, 0.95));
        std::printf("average lap s  p5 %.1f  p50 %.1f  p95 %.1f\n", pct(al, 0.05), pct(al, 0.5), pct(al, 0.95));
        std::printf("bad lap s      p5 %.1f  p50 %.1f  p95 %.1f\n", pct(bl, 0.05), pct(bl, 0.5), pct(bl, 0.95));
        std::printf("repairs: %d total, %d courses needed >=1 repair round\n", rep, repCourses);
        std::printf("final validation (32 runs x 3 laps per course): catch-net respawns %ld in %d courses; no-net falls (checkpoint restart) %ld in %d courses\n",
                    nets, netCourses, falls, fallCourses);
        std::printf("gen time ms    p50 %.0f  p95 %.0f  max %.0f\n", pct(ms, 0.5), pct(ms, 0.95), pct(ms, 1.0));
        std::printf("play mesh: collision triangles p50 %.0f  p95 %.0f  max %.0f (the game's pool 8191); pieces max %.0f (64); steepest walkable "
                    "deck facet p95 %.1f  max %.1f deg; largest slope change a station p95 %.3f  max %.3f\n", pct(pmT, 0.5), pct(pmT, 0.95),
                    pct(pmT, 1.0), pct(pmP, 1.0), pct(pmS, 0.95), pct(pmS, 1.0), pct(pmC, 0.95), pct(pmC, 1.0));
        std::printf("game objects: rings a course p5 %.0f  p50 %.0f  p95 %.0f (%d courses without); springs %ld of %ld spring modules (%ld flights left out); "
                    "dash panels %ld; dash ramps %ld (%ld left out); grind rails %ld (%ld too steep: strip only)\n", pct(obR, 0.05), pct(obR, 0.5),
                    pct(obR, 0.95), noRings, obS, obSM, obSO, obD, obRp, obRpO, obRl, obRlO);
        std::printf("validator flags per module (all candidates, before repair):");
        for (auto& kv : failures) std::printf("  %s %d", kv.first.c_str(), kv.second);
        std::printf("\nmodule usage in final courses:");
        for (auto& kv : use) std::printf("  %s %d", kv.first.c_str(), kv.second);
        std::printf("\n\n");
    }
    if (!csv.empty()) {
        FILE* f = std::fopen(csv.c_str(), "w");
        if (f) {
            std::fprintf(f, "room,type,scale_cm,boxes,ok,fallback,completable,clearance_ok,skeleton,length_m,expert_lap_s,avg_lap_s,bad_lap_s,repairs,modules,nets,gen_ms\n");
            for (auto& r : rows)
                std::fprintf(f, "%d,%s,%.0f,%d,%d,%d,%d,%d,%s,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%.1f\n", r.room, roomTypeName(r.type), r.scaleCm,
                             r.nBoxes, r.ok, r.fallback, r.completable, r.clearanceOk, r.kind.c_str(), r.lengthM, r.expertLap,
                             r.avgLap, r.badLap, r.repairs, r.modules, r.nets, r.ms);
            std::fclose(f);
        }
    }
    return 0;
}
