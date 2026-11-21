// roomrun-batch: generate courses for many random rooms and report robustness statistics.
//   roomrun-batch --rooms 1000 [--seed S] [--threads T] [--csv out.csv]
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "roomrun/generator.hpp"

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
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--rooms") rooms = std::atoi(next().c_str());
        else if (a == "--seed") seed = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--threads") threads = std::atoi(next().c_str());
        else if (a == "--csv") csv = next();
        else if (a == "--dump-fallbacks") dumpDir = next();
        else { std::fprintf(stderr, "usage: roomrun-batch --rooms N [--seed S] [--threads T] [--csv f]\n"); return 2; }
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
        std::vector<double> len, el, al, bl, ms;
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
