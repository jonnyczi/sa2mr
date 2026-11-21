// roomrun-gen: generate one course for a room.
//   roomrun-gen --room room.json --seed N --out course.json --svg course.svg --obj course.obj
//               [--modules data/modules.json] [--sonic-cm 10] [--speed-scale 0.6]
//               [--random rectangle|l-shape|hallway|irregular --room-seed K --save-room room.json]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "roomrun/export.hpp"
#include "roomrun/generator.hpp"

using namespace rr;

static void usage() {
    std::fprintf(stderr,
                 "usage: roomrun-gen (--room room.json | --random TYPE [--room-seed K]) [--seed N]\n"
                 "                   [--out course.json] [--svg course.svg] [--obj course.obj] [--save-room r.json]\n"
                 "                   [--modules modules.json] [--sonic-cm 10] [--speed-scale 0.6] [--quiet] [--debug]\n"
                 "                   [--prefer perimeter|figure8|helix|circle]\n");
}

int main(int argc, char** argv) {
    std::string roomPath, outPath, svgPath, objPath, modulesPath, randomType, saveRoom;
    uint64_t seed = 1, roomSeed = 1;
    GenParams gp;
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            return argv[++i];
        };
        if (a == "--room") roomPath = next();
        else if (a == "--seed") seed = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--out") outPath = next();
        else if (a == "--svg") svgPath = next();
        else if (a == "--obj") objPath = next();
        else if (a == "--modules") modulesPath = next();
        else if (a == "--sonic-cm") gp.sonicHeightM = std::atof(next().c_str()) / 100.0;
        else if (a == "--speed-scale") gp.speedScale = std::atof(next().c_str());
        else if (a == "--random") randomType = next();
        else if (a == "--room-seed") roomSeed = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--save-room") saveRoom = next();
        else if (a == "--quiet") quiet = true;
        else if (a == "--debug") gp.debug = true;
        else if (a == "--prefer") gp.preferSkeleton = next();
        else { usage(); return 2; }
    }
    Room room;
    if (!roomPath.empty()) {
        std::string err, txt = readFile(roomPath);
        if (txt.empty()) { std::fprintf(stderr, "cannot read %s\n", roomPath.c_str()); return 1; }
        Json j = Json::parse(txt, &err);
        if (!err.empty()) { std::fprintf(stderr, "%s: %s\n", roomPath.c_str(), err.c_str()); return 1; }
        room = roomFromJson(j);
    } else if (!randomType.empty()) {
        RoomType t = RoomType::Rectangle;
        for (RoomType rt : {RoomType::Rectangle, RoomType::LShape, RoomType::Hallway, RoomType::Irregular})
            if (randomType == roomTypeName(rt)) t = rt;
        Rng rng(roomSeed);
        room = randomRoom(rng, t);
    } else { usage(); return 2; }
    if (!saveRoom.empty()) writeFile(saveRoom, roomToJson(room).dump(3, true) + "\n");

    ModuleLibrary lib;
    if (!modulesPath.empty()) {
        std::string err;
        Json lj = Json::parse(readFile(modulesPath), &err);
        applyPhysicsOverrides(gp.physics, lj);
        lib = ModuleLibrary::fromJson(lj, gp.speedScale, &err);
        if (!err.empty() || lib.defs.empty()) { std::fprintf(stderr, "modules: %s\n", err.c_str()); return 1; }
    } else {
        lib = ModuleLibrary::builtin(gp.speedScale);
    }

    GenResult r = generateCourse(room, seed, gp, lib);
    if (!r.ok) { std::fprintf(stderr, "generation failed: %s\n", r.error.c_str()); return 1; }
    if (!outPath.empty()) writeFile(outPath, courseToJson(r, lib, gp, seed).dump(4, true) + "\n");
    if (!svgPath.empty()) writeFile(svgPath, courseToSvg(r, lib, gp));
    if (!objPath.empty()) writeFile(objPath, courseToObj(r, lib, gp));
    if (!quiet) {
        const Course& c = r.course;
        std::printf("skeleton=%s length=%.2fm modules=%zu fallback=%s repairs=%d candidates=%d time=%.0fms\n",
                    c.kind.c_str(), c.sk.lengthM(), c.mods.size(), c.fallback ? "yes" : "no", c.repairs,
                    r.candidatesTried, r.genMs);
        std::printf("bots: %d runs, %d failed, net catches=%d, no-net falls=%d | lap expert %.2fs average %.2fs bad %.2fs | ranks A %.1fs B %.1fs C %.1fs\n",
                    c.stats.runs, c.stats.failedRuns, c.stats.netCatches, c.stats.falls, c.stats.expertLap, c.stats.averageLap,
                    c.stats.badLap, c.rankA, c.rankB, c.rankC);
        std::printf("sequence:");
        for (auto& m : c.mods) std::printf(" %s", lib.defs[m.def].id.c_str());
        std::printf("\n");
        for (auto& l : r.repairLog) std::printf("repair: %s\n", l.c_str());
        auto issues = checkClearance(c, r.room, gp);
        for (auto& s : issues) std::printf("CLEARANCE: %s\n", s.c_str());
    }
    return 0;
}
