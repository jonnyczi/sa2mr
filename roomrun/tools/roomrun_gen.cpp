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
#include "roomrun/objects.hpp"
#include "roomrun/metrics.hpp"
#include "roomrun/playmesh.hpp"
#include "preview.hpp"

using namespace rr;

static void usage() {
    std::fprintf(stderr,
                 "usage: roomrun-gen (--room room.json | --random TYPE [--room-seed K]) [--seed N]\n"
                 "                   [--out course.json] [--svg course.svg] [--obj course.obj] [--save-room r.json]\n"
                 "                   [--modules modules.json] [--sonic-cm 10] [--speed-scale 0.6] [--quiet] [--debug]\n"
                 "                   [--prefer perimeter|figure8|helix|circle|coil] [--play-obj playable.obj] [--objects]\n"
                 "                   [--area W,D,UX,UY (a play-area rectangle about the origin, the user at UX,UY)] [--player-r R]\n"
                 "                   [--ppm preview.ppm (3D preview: a corner view and a side view)] [--mesh-bots]\n");
}

int main(int argc, char** argv) {
    std::string areaSpec, ppmPath;
    bool meshBots = false;
    double playerR = -1;
    std::string roomPath, outPath, svgPath, objPath, playObjPath, modulesPath, randomType, saveRoom;
    bool listObjects = false;
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
        else if (a == "--play-obj") playObjPath = next();
        else if (a == "--objects") listObjects = true;
        else if (a == "--area") areaSpec = next();
        else if (a == "--player-r") playerR = std::atof(next().c_str());
        else if (a == "--ppm") ppmPath = next();
        else if (a == "--mesh-bots") meshBots = true;
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
    } else if (!areaSpec.empty()) {
        double w = 2, d = 2, ux = 0, uy = 0;
        if (std::sscanf(areaSpec.c_str(), "%lf,%lf,%lf,%lf", &w, &d, &ux, &uy) < 2) { usage(); return 2; }
        room.floor = {{-w / 2, -d / 2}, {w / 2, -d / 2}, {w / 2, d / 2}, {-w / 2, d / 2}};
        room.player = {ux, uy};
        room.playerRadius = w < 2.5 || d < 2.5 ? 0.3 : 0.7;   // the VR player's rule (host/xr README "Room Run v0")
    } else if (!randomType.empty()) {
        RoomType t = RoomType::Rectangle;
        for (RoomType rt : {RoomType::Rectangle, RoomType::LShape, RoomType::Hallway, RoomType::Irregular})
            if (randomType == roomTypeName(rt)) t = rt;
        Rng rng(roomSeed);
        room = randomRoom(rng, t);
    } else { usage(); return 2; }
    if (playerR > 0) room.playerRadius = playerR;
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
    if (!playObjPath.empty()) {
        PlayMesh pm = buildPlayMesh(r, lib, gp);
        writeFile(playObjPath, playMeshToObj(pm));
        if (!quiet)
            std::printf("playable mesh: %zu stations, %zu collision chunks, %zu collision / %zu drawn triangles, %d bridged, %d raised, %d rail stations\n",
                        pm.st.size(), pm.chunks.size(), pm.collisionTriangles(), pm.drawTriangles(), pm.bridgedStations,
                        pm.raisedStations, pm.railStations);
    }
    if (meshBots) {   // the bots on the play mesh's profile (its heights where the generator has a surface), as the tests run them
        const PlayMesh pm = buildPlayMesh(r, lib, gp);
        Course c2 = r.course;
        for (int j = 0; j < c2.sk.size(); ++j) if (c2.exists[j] && !c2.hole[j]) c2.z[j] = pm.st[j].p.y;
        GenParams g2 = gp;
        g2.debug = true;
        const ValidationReport rep = validateCourse(c2, lib, g2, r.course.seed);
        std::printf("bots on the play mesh: %d of %d runs failed\n", rep.failedRuns, rep.runs);
        for (auto& s : rep.failReasons) std::printf("  %s\n", s.c_str());
    }
    if (!ppmPath.empty()) {
        const PlayMesh pm = buildPlayMesh(r, lib, gp);
        const CourseObjects co = placeObjects(r, pm, lib, gp);
        if (!rrpreview::renderPreview(r, pm, co, ppmPath)) std::fprintf(stderr, "cannot write %s\n", ppmPath.c_str());
    }
    if (listObjects) {   // the game objects on the play mesh (Room Run in the game)
        const PlayMesh pm = buildPlayMesh(r, lib, gp);
        const CourseObjects co = placeObjects(r, pm, lib, gp);
        std::printf("objects: %s\n", objectSummary(co).c_str());
        for (auto& o : co.objs)
            std::printf("  %-11s station %3d at %.3f %.3f %.3f m dir %.2f %.2f count %d launch %.2f %.2f %.2f speed %.1f lands %d apex %.1f\n",
                        objKindName(o.kind), o.station, o.p.x, o.p.y, o.p.z, o.dir.x, o.dir.z, o.count, o.launchU.x, o.launchU.y, o.launchU.z,
                        o.speedU, o.landStation, o.apexU);
        for (auto& rl : co.rails) std::printf("  rail stations %d..%d, %zu points, climbs %.1f units\n", rl.s0, rl.s1, rl.pts.size(), rl.riseU);
        for (auto& s : co.stats.notes) std::printf("  left out: %s\n", s.c_str());
    }
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
        const ShapeMetrics sh = shapeMetrics(r, buildPlayMesh(r, lib, gp));
        std::printf("shape: lap %.2f m on %.2f m2 (%.2f m/m2), decks %.2f..%.2f m (mean %.2f), %d tiers, %d crossings, %.0f%% stacked, smallest stacked gap %.1f units\n",
                    sh.lapM, sh.floorM2, sh.lapPerM2, sh.minDeckM, sh.maxDeckM, sh.meanDeckM, sh.tiers, sh.crossings, sh.stackedShare * 100, sh.minStackGapU);
    }
    return 0;
}
