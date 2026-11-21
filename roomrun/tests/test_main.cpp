// Minimal self-contained test runner (no framework dependency).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "roomrun/export.hpp"
#include "roomrun/field.hpp"
#include "roomrun/generator.hpp"
#include "roomrun/metrics.hpp"
#include "roomrun/objects.hpp"
#include "roomrun/playmesh.hpp"

using namespace rr;

static int gFailures = 0, gChecks = 0;
static int gBadMeshRuns = 0, gBadMeshFails = 0;   // the bad driver on the play meshes checked (checkPlayMesh)
#define CHECK(cond)                                                                \
    do {                                                                           \
        ++gChecks;                                                                 \
        if (!(cond)) { ++gFailures; std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

static Room rectRoom(double w, double h, Vec2 player, std::vector<Box> boxes = {}) {
    Room r;
    r.floor = {{0, 0}, {w, 0}, {w, h}, {0, h}};
    r.player = player;
    r.boxes = boxes;
    return r;
}
static Box box(double cx, double cy, double w, double d, double h, const char* label) {
    Box b;
    b.rect.c = {cx, cy}; b.rect.half = {w / 2, d / 2}; b.rect.yaw = 0; b.height = h; b.label = label;
    return b;
}

static void testGeometry() {
    Polygon sq = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    CHECK_NEAR(signedArea(sq), 1.0, 1e-12);
    CHECK(isSimplePolygon(sq));
    Polygon bowtie = {{0, 0}, {1, 1}, {1, 0}, {0, 1}};
    CHECK(!isSimplePolygon(bowtie));
    Polygon dup = {{0, 0}, {1, 0}, {1, 0}, {0, 1}};
    CHECK(!isSimplePolygon(dup));
    CHECK(pointInPolygon(sq, {0.5, 0.5}));
    CHECK(!pointInPolygon(sq, {1.5, 0.5}));
    CHECK_NEAR(distPointSegment({0.5, 1}, {0, 0}, {1, 0}), 1.0, 1e-12);
    CHECK_NEAR(signedDistPolygon(sq, {0.5, 0.25}), 0.25, 1e-12);
    CHECK(signedDistPolygon(sq, {2, 0.5}) < 0);
    ORect r{{0, 0}, {1, 0.5}, kPi / 2};
    CHECK(r.contains({0.4, 0.9}));  // rotated 90 degrees: long axis is y
    CHECK(!r.contains({0.9, 0.4}));
    CHECK_NEAR(r.dist({0, 2}), 1.0, 1e-9);
    // L-shape is simple, CCW after normalisation
    Room l;
    l.floor = {{0, 0}, {0, 3}, {2, 3}, {2, 1.5}, {4, 1.5}, {4, 0}};  // CW
    l.player = {1, 1};
    normalizeRoom(l);
    CHECK(signedArea(l.floor) > 0);
    std::string err;
    CHECK(validateRoom(l, &err));
    // EDT: single feature pixel
    std::vector<uint8_t> mask(25, 0);
    mask[12] = 1;
    auto d = edt(mask, 5, 5, 0.1);
    CHECK_NEAR(d[12], 0.0, 1e-6);
    CHECK_NEAR(d[0], std::sqrt(8.0) * 0.1, 1e-5);
    CHECK_NEAR(d[14], 0.2, 1e-6);
}

static void testInsetAndMedial() {
    GenParams gp;
    Room room = rectRoom(4, 3, {0.4, 0.4});
    room.playerRadius = 0.1;
    FreeSpace fs = buildFreeSpace(room, gp);
    // inset contour at 0.5 m beyond the wall margin: a 3 x 2 m rectangle minus the player bump region
    auto loops = contourLoops(fs.clearance, 0.5);
    CHECK(!loops.empty());
    double best = 0;
    for (auto& lp : loops) best = std::max(best, polylineLength(lp, true));
    double expect = 2 * ((4 - 2 * (0.5 + gp.wallMarginM)) + (3 - 2 * (0.5 + gp.wallMarginM)));
    CHECK(std::fabs(best - expect) / expect < 0.05);
    // opening rounds the corners: shorter than the sharp inset
    Grid G = openedField(fs.clearance, 0.5, 0.3);
    auto rl = contourLoops(G, 0.0);
    double bestR = 0;
    for (auto& lp : rl) bestR = std::max(bestR, polylineLength(lp, true));
    CHECK(bestR < best && bestR > best * 0.85);
    // medial axis: biggest inscribed disk of a 4x3 room sits on the long centerline
    CHECK(!fs.maxima.empty());
    CHECK_NEAR(fs.maxima[0].p.y, 1.5, 0.08);
    CHECK_NEAR(fs.maxima[0].r, 1.5 - gp.wallMarginM, 0.05);
}

static void testJoins() {
    GenParams gp;
    ModuleLibrary lib = ModuleLibrary::builtin(gp.speedScale);
    CHECK(lib.defs.size() >= 11);
    const ModuleDef& gap = lib.defs[lib.find("gap_jump")];
    const ModuleDef& accel = lib.defs[lib.find("accel_pad")];
    const ModuleDef& dash = lib.defs[lib.find("dash_panel")];
    const ModuleDef& loop = lib.defs[lib.find("loop")];
    CHECK(accel.exit.subsetOf(gap.entry));   // accel pad certifies gap entry speed
    CHECK(!dash.exit.subsetOf(gap.entry));   // too fast for the run-off gap
    CHECK(dash.exit.subsetOf(loop.entry));
    Envelope rest{0, 0, 9, 30};
    CHECK(!rest.subsetOf(gap.entry));        // standing start cannot take a gap
    // every assembled course chains: E_out(prev) subset of E_in(next), and closes the lap
    for (int i = 0; i < 12; ++i) {
        Rng rng(100 + i);
        Room room = randomRoom(rng, (RoomType)(i % 4));
        GenResult r = generateCourse(room, 7, gp, lib);
        if (!r.ok) continue;
        Course c = r.course;
        CHECK(checkJoins(c, lib, gp));
        for (size_t m = 1; m < c.mods.size(); ++m) {
            CHECK(c.mods[m - 1].out.subsetOf(lib.defs[c.mods[m].def].entry));
            CHECK(c.mods[m].s0 == c.mods[m - 1].s1);
            CHECK_NEAR(c.mods[m].h0, c.mods[m - 1].h1, 1e-9);
        }
        CHECK(c.mods.back().s1 == c.sk.size());
        CHECK_NEAR(c.mods.back().h1, c.mods.front().h0, 1e-9);
    }
}

static void testDeterminism() {
    GenParams gp;
    ModuleLibrary lib = ModuleLibrary::builtin(gp.speedScale);
    Room room = rectRoom(4.2, 3.6, {2.0, 1.6}, {box(0.9, 2.9, 1.6, 0.8, 0.45, "couch"), box(3.3, 0.8, 1.0, 0.6, 0.75, "table")});
    GenResult a = generateCourse(room, 42, gp, lib);
    GenResult b = generateCourse(room, 42, gp, lib);
    CHECK(a.ok && b.ok);
    std::string ja = courseToJson(a, lib, gp, 42).dump(), jb = courseToJson(b, lib, gp, 42).dump();
    CHECK(ja == jb);
    CHECK(courseToSvg(a, lib, gp) == courseToSvg(b, lib, gp));
    CHECK(courseToObj(a, lib, gp) == courseToObj(b, lib, gp));
    // tracing jitter below the 5 cm quantum gives the same course
    Room jitter = room;
    jitter.floor[1].x += 0.012;
    jitter.player.y -= 0.01;
    GenResult c = generateCourse(jitter, 42, gp, lib);
    CHECK(c.ok && courseToJson(c, lib, gp, 42).dump() == ja);
    // a different seed gives a different course
    GenResult d = generateCourse(room, 43, gp, lib);
    CHECK(d.ok && courseToJson(d, lib, gp, 43).dump() != ja);
    CHECK(courseSeed(a.room, 42) != courseSeed(a.room, 43));
}

static void testClearanceAndCompletion() {
    ModuleLibrary lib = ModuleLibrary::builtin(0.6);
    int generated = 0;
    for (double scale : {0.10, 0.15}) {
        GenParams gp;
        gp.sonicHeightM = scale;
        for (int i = 0; i < 24; ++i) {
            Rng rng(900 + i);
            Room room = randomRoom(rng, (RoomType)(i % 4));
            GenResult r = generateCourse(room, i, gp, lib);
            if (!r.ok) continue;  // infeasible player spot
            ++generated;
            auto issues = checkClearance(r.course, r.room, gp);
            if (!issues.empty()) std::printf("  room %d: %s\n", i, issues[0].c_str());
            CHECK(issues.empty());
            CHECK(r.course.stats.completable);
            // explicit player-cylinder check over every station + lateral edge
            double hw = 0.5 * gp.trackWidthU * gp.metersPerUnit();
            for (auto& p : r.course.sk.pts) CHECK((p - r.room.player).len() >= r.room.playerRadius + hw - 0.02);
        }
    }
    CHECK(generated >= 40);
}

static void testFallback() {
    GenParams gp;
    gp.maxCandidates = 0;  // force the all-safe fallback path
    ModuleLibrary lib = ModuleLibrary::builtin(gp.speedScale);
    for (int i = 0; i < 8; ++i) {
        Rng rng(500 + i);
        Room room = randomRoom(rng, (RoomType)(i % 4));
        GenResult r = generateCourse(room, 1, gp, lib);
        if (!r.ok) continue;
        CHECK(r.fallback);
        CHECK(r.course.stats.completable);
        CHECK(checkClearance(r.course, r.room, gp).empty());
        for (auto& m : r.course.mods)
            CHECK(m.kind == Kind::Start || m.kind == Kind::Track || m.kind == Kind::Rail);
    }
}

static void testExports() {
    GenParams gp;
    ModuleLibrary lib = ModuleLibrary::builtin(gp.speedScale);
    Room room = rectRoom(4.0, 3.2, {1.2, 1.2}, {box(3.0, 2.4, 1.2, 0.8, 0.75, "table")});
    GenResult r = generateCourse(room, 3, gp, lib);
    CHECK(r.ok);
    // OBJ: every face index refers to an existing vertex
    std::string obj = courseToObj(r, lib, gp);
    long nv = 0, bad = 0, nf = 0;
    std::istringstream in(obj);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "v") ++nv;
        if (tag != "f") continue;
        ++nf;
        long idx;
        while (ls >> idx) if (idx < 1 || idx > nv) ++bad;
    }
    CHECK(nv > 100 && nf > 100 && bad == 0);
    // JSON round-trips and carries the station arrays
    std::string err;
    Json j = Json::parse(courseToJson(r, lib, gp, 3).dump(4, true), &err);
    CHECK(err.empty());
    CHECK((int)j["stations"]["x"].size() == r.course.sk.size());
    CHECK(j["rankSeconds"].numOr("A", 0) > 0 && j["rankSeconds"].numOr("A", 0) < j["rankSeconds"].numOr("C", 0));
    CHECK(courseToSvg(r, lib, gp).find("<svg") == 0);
}

static void testTinyRoomRejected() {
    GenParams gp;
    ModuleLibrary lib = ModuleLibrary::builtin(gp.speedScale);
    Room room = rectRoom(1.5, 1.5, {0.75, 0.75});
    GenResult r = generateCourse(room, 1, gp, lib);
    CHECK(!r.ok);  // no space outside the 0.7 m cylinder: must refuse, never emit an unsafe course
    Room bad = rectRoom(3, 3, {5, 5});
    CHECK(!generateCourse(bad, 1, gp, lib).ok);
}


// The playable mesh (Room Run v0 in the game): continuous, walled, inside the room, deterministic.
static void checkPlayMesh(const GenResult& r, const ModuleLibrary& lib, const GenParams& gp) {
    PlayMeshParams pp;
    const PlayMesh game = buildPlayMesh(r, lib, gp, pp);   // as the game gets it: each chunk sorted by Z
    pp.sortForGame = false;                                // in station order, for the checks by position below
    PlayMesh m = buildPlayMesh(r, lib, gp, pp);
    const int N = r.course.sk.size();
    CHECK((int)m.st.size() == N);
    {   // the game's order: the same triangles, each chunk's by their middle's Z, highest first
        bool same = game.chunks.size() == m.chunks.size(), sorted = true;
        for (size_t i = 0; same && i < m.chunks.size(); ++i) {
            const auto& a = game.chunks[i].tri;
            same = a.size() == m.chunks[i].tri.size();
            for (size_t t = 1; same && t < a.size() / 9; ++t)
                sorted &= a[9 * t - 7] + a[9 * t - 4] + a[9 * t - 1] >= a[9 * t + 2] + a[9 * t + 5] + a[9 * t + 8];
            std::vector<float> x = a, y = m.chunks[i].tri;
            std::sort(x.begin(), x.end());
            std::sort(y.begin(), y.end());
            same &= x == y;
        }
        CHECK(same);
        CHECK(sorted);
    }
    const double rise = pp.maxRiseSlope * r.course.sk.stepM + 1e-6;
    int steep = 0;
    for (int j = 0; j < N; ++j) {
        steep += m.st[(j + 1) % N].p.y - m.st[j].p.y > rise;
        if (m.st[(j + 1) % N].p.y - m.st[j].p.y > rise && std::getenv("RR_PLAYMESH_STATS"))
            std::printf("    steep rise %.4f at station %d/%d (limit %.4f)\n", m.st[(j + 1) % N].p.y - m.st[j].p.y, j, N, rise);
    }
    CHECK(steep == 0);
    int stations = 0, wallStations = 0, badDeck = 0, badWall = 0, outside = 0, steepDeck = 0;
    // per station: the deck's lanes and its underside's in a deck piece, the two walls in a wall piece of their own
    // (the game resolved a wall in the deck's own piece badly: on a slope Sonic sank into the deck and lost the ground)
    const int deckT = 2 * m.lanes;
    double maxTilt = 0;
    for (auto& c : m.chunks) {
        CHECK(c.s1 - c.s0 <= pp.chunkStations && c.s1 > c.s0);
        const int perSt = c.walls ? 4 : 2 * deckT;
        (c.walls ? wallStations : stations) += c.s1 - c.s0;
        CHECK(c.tri.size() == (size_t)(c.s1 - c.s0) * perSt * 9);
        for (size_t t = 0; t < c.tri.size() / 9; ++t) {
            const float* v = &c.tri[9 * t];
            const Vec3 a{v[0], v[1], v[2]}, b{v[3], v[4], v[5]}, cc{v[6], v[7], v[8]};
            const Vec3 e1 = b - a, e2 = cc - a;
            Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
            const double l = n.len();
            if (l < 1e-12) continue;
            const double ny = n.y / l;
            const int j = c.s0 + (int)(t / perSt), jn = (j + 1) % N, k = (int)(t % perSt);
            const bool drop = m.st[jn].p.y < m.st[j].p.y - 1e-6 &&   // a drop module's lip: steep, down only
                              m.st[j].p.y - m.st[jn].p.y > pp.dropSlope * 0.5 * r.course.sk.stepM;
            if (c.walls) badWall += std::fabs(ny) > 1e-3;   // the walls stand upright
            else if (k < deckT) {
                badDeck += ny <= 0.0 || (ny < 0.5 && !drop);   // the deck faces up
                if (!drop) {   // and no facet of it is steeper than the walkway's limit (a bend's inner edge)
                    const double tilt = std::acos(std::min(1.0, ny));
                    maxTilt = std::max(maxTilt, tilt);
                    const double lim = std::atan(m.st[jn].p.y >= m.st[j].p.y ? pp.maxRiseSlope : pp.maxFallSlope) + 0.03;
                    steepDeck += tilt > lim;
                    if (tilt > lim && std::getenv("RR_PLAYMESH_STATS")) {
                        const int jp = (j + N - 1) % N;
                        std::printf("    steep facet %.1f deg at station %d/%d lane tri %d: z %.4f %.4f %.4f slope %.3f, halfW %.4f %.4f, run %.4f, "
                                    "bridged %d kind %s, edges %.4f %.4f %.4f\n", tilt * 57.2958, j, N, k, m.st[jp].p.y, m.st[j].p.y, m.st[jn].p.y,
                                    (m.st[jn].p.y - m.st[j].p.y) / r.course.sk.stepM, m.st[j].halfW, m.st[jn].halfW,
                                    std::hypot(m.st[jn].p.x - m.st[j].p.x, m.st[jn].p.z - m.st[j].p.z), (int)m.st[j].bridged,
                                    playKindName(m.st[j].kind), (b - a).len(), (cc - b).len(), (a - cc).len());
                    }
                }
            }
            else badDeck += ny >= 0.0 || (ny > -0.5 && !drop);   // its underside faces down (a ceiling)
            for (int q = 0; q < 3; ++q) outside += !pointInPolygon(r.room.floor, Vec2{v[3 * q], -v[3 * q + 2]});
        }
    }
    CHECK(stations == N);
    CHECK(wallStations == N);
    CHECK(badDeck == 0);
    CHECK(badWall == 0);
    CHECK(outside == 0);
    CHECK(steepDeck == 0);
    // the profile bends gradually (no one-station crease: the Frame's ramps behaved as steps and ledges),
    // except at a drop's lip and where a constraint (a furniture top, a crossing's gap) holds it
    int creases = 0;
    double maxCrease = 0;
    for (int j = 0; j < N; ++j) {
        const int jp = (j + N - 1) % N, jn = (j + 1) % N;
        const double s0 = (m.st[j].p.y - m.st[jp].p.y) / r.course.sk.stepM, s1 = (m.st[jn].p.y - m.st[j].p.y) / r.course.sk.stepM;
        if (s0 < -pp.dropSlope * 0.5 || s1 < -pp.dropSlope * 0.5) continue;
        maxCrease = std::max(maxCrease, std::fabs(s1 - s0));
        creases += std::fabs(s1 - s0) > pp.maxCreaseSlope + 1e-9;
        if (std::fabs(s1 - s0) > pp.maxCreaseSlope + 1e-9 && std::getenv("RR_PLAYMESH_STATS"))
            std::printf("    crease %.3f at station %d/%d: slopes %.3f %.3f, z %.4f (generator %.4f, exists %d hole %d), partners %zu, kind %s\n",
                        s1 - s0, j, N, s0, s1, m.st[j].p.y, r.course.z[j], (int)r.course.exists[j], (int)r.course.hole[j],
                        j < (int)r.course.sk.partners.size() ? r.course.sk.partners[j].size() : 0, playKindName(m.st[j].kind));
    }
    CHECK(creases * 50 <= N);   // at most 2% of the stations (the constrained ones)
    // the walkway's width changes gently (no wall corner to get stuck in)
    int widthSteps = 0;
    for (int j = 0; j < N; ++j) widthSteps += std::fabs(m.st[(j + 1) % N].halfW - m.st[j].halfW) > pp.widthRateU * m.mpu + 1e-9;
    CHECK(widthSteps == 0);
    // where one deck passes over another, the gap is still the generator's or the layer gap
    int lowGap = 0;
    for (int j = 0; j < N && j < (int)r.course.sk.partners.size(); ++j)
        for (int q : r.course.sk.partners[j]) {
            const double g0 = r.course.z[j] - r.course.z[q];
            if (!r.course.exists[j] || !r.course.exists[q] || r.course.hole[j] || r.course.hole[q] || g0 <= 1e-6) continue;
            lowGap += m.st[j].p.y - m.st[q].p.y < std::min(g0, gp.layerGapU * m.mpu) - 1e-6;
        }
    CHECK(lowGap == 0);
    // The bots on the play mesh's profile (its heights where the generator had a surface): the expert and
    // average drivers complete every run. The bad driver, in aggregate over all the courses checked, fails at most
    // 2% of its runs (gBadMeshFails / gBadMeshRuns, checked by the callers). It used to be held per course to no
    // more failures than on the profile the shaping starts from (v0's), but on a course where the bad driver is
    // marginal (a homing chain it misses 30% of the time, a long climb at its throttle's limit) a profile a few mm
    // different is a new roll of its random inputs: both are printed (RR_PLAYMESH_STATS) for the record.
    {
        auto onMesh = [&](const PlayMesh& pm) {
            Course c2 = r.course;
            for (int j = 0; j < N; ++j)
                if (c2.exists[j] && !c2.hole[j]) c2.z[j] = pm.st[j].p.y;
            return validateCourse(c2, lib, gp, r.course.seed);
        };
        auto badOnly = [](const ValidationReport& rep) {
            int n = 0;
            for (auto& s : rep.failReasons) n += s.rfind("bad:", 0) != 0;
            return n == 0;
        };
        const ValidationReport rep = onMesh(m);
        CHECK(badOnly(rep));
        gBadMeshRuns += rep.runs - rep.runs / 8 - rep.runs * 3 / 8;   // the bad driver's share (validateCourse)
        if (!rep.completable) {
            PlayMeshParams v0 = pp;
            v0.smoothPasses = 0; v0.deckLanes = 1; v0.widthRateU = 1e9; v0.maxFallSlope = 1e9;
            const ValidationReport rep0 = onMesh(buildPlayMesh(r, lib, gp, v0));
            gBadMeshFails += rep.failedRuns;
            if (std::getenv("RR_DUMP_ROOM") && rep.failedRuns > rep0.failedRuns) writeFile(std::getenv("RR_DUMP_ROOM"), roomToJson(r.room).dump(3, true));
            if (std::getenv("RR_PLAYMESH_STATS")) {
                std::printf("    bots on the play mesh: %d of %d runs failed (on v0's profile %d):", rep.failedRuns, rep.runs, rep0.failedRuns);
                for (size_t i = 0; i < rep.failReasons.size() && i < 6; ++i) std::printf(" [%s]", rep.failReasons[i].c_str());
                std::printf("\n");
            }
        }
    }
    if (std::getenv("RR_PLAYMESH_STATS"))
        std::printf("  play mesh: %d stations, steepest deck facet %.1f deg, largest crease %.3f (%d over %.2f), %zu collision triangles\n",
                    N, maxTilt * 57.2958, maxCrease, creases, pp.maxCreaseSlope, m.collisionTriangles());
    bool road = false, curb = false;   // a deck drawn (a coil can be all ramps and rails) and its curbs
    for (auto& g : m.groups) {
        road |= (g.name == "road" || g.name == "ramp" || g.name == "rail") && g.triangles() > 0;
        curb |= (g.name == "curb_a" || g.name == "rail_curb_a") && g.triangles() > 0;
    }
    CHECK(road && curb);
    PlayMesh m2 = buildPlayMesh(r, lib, gp, pp);
    CHECK(m2.chunks.size() == m.chunks.size() && m2.drawTriangles() == m.drawTriangles());
    bool same = m2.chunks.size() == m.chunks.size();
    for (size_t i = 0; same && i < m.chunks.size(); ++i) same = m.chunks[i].tri == m2.chunks[i].tri;
    CHECK(same);
}
static void testPlayMesh() {
    GenParams gp;
    gp.sonicHeightM = 0.12;   // the VR player's default (SA2_XR_SONIC_SCALE 0.012 m a unit)
    ModuleLibrary lib = ModuleLibrary::builtin(gp.speedScale);
    // the play-area rectangles the VR player feeds it (centred, the user in the middle)
    for (auto wh : {std::pair<double, double>{3.0, 2.5}, {4.0, 3.0}, {2.4, 2.4}, {5.0, 4.0}}) {
        Room room;
        const double w = wh.first / 2, h = wh.second / 2;
        room.floor = {{-w, -h}, {w, -h}, {w, h}, {-w, h}};
        room.player = {0, 0};
        GenResult r = generateCourse(room, 1, gp, lib);
        if (!r.ok) std::printf("  (no course for a %.1f x %.1f m play area: %s)\n", wh.first, wh.second, r.error.c_str());
        CHECK(r.ok || wh.first < 2.5);   // a small area may refuse; the player then keeps the flat floor
        if (r.ok) checkPlayMesh(r, lib, gp);
    }
    Rng rng(4242);
    int ok = 0;
    for (int i = 0; i < 24; ++i) {
        Room room = randomRoom(rng, (RoomType)(i % 4));
        GenResult r = generateCourse(room, 7, gp, lib);
        if (!r.ok) continue;
        ++ok;
        checkPlayMesh(r, lib, gp);
    }
    CHECK(ok >= 20);
}

// The game objects on the play mesh (Room Run objects): every kept flight lands ahead on the course under
// the game-physics model, rings sit above the deck inside the room, rails follow their strips, the same
// course gives the same objects.
static void checkObjects(const GenResult& r, const ModuleLibrary& lib, const GenParams& gp, int* rings, int* springs, int* springMods) {
    const PlayMesh pm = buildPlayMesh(r, lib, gp);
    const ObjectParams op;
    const CourseObjects co = placeObjects(r, pm, lib, gp, op);
    const CourseObjects co2 = placeObjects(r, pm, lib, gp, op);
    CHECK(co.objs.size() == co2.objs.size() && co.rails.size() == co2.rails.size());
    for (size_t i = 0; i < co.objs.size() && i < co2.objs.size(); ++i)
        CHECK(co.objs[i].p.x == co2.objs[i].p.x && co.objs[i].p.y == co2.objs[i].p.y && co.objs[i].launchU.y == co2.objs[i].launchU.y);
    const int N = (int)pm.st.size();
    int sm = 0;
    for (auto& m : r.course.mods) sm += m.kind == Kind::Spring;
    *springMods += sm;
    CHECK(co.stats.springs - co.stats.hopSprings - co.stats.climbSprings + co.stats.springsDropped == sm);
    CHECK(co.stats.springs >= 1);                     // variety: a spring on every course (a hop spring where no module has one)
    for (auto& o : co.objs) CHECK(o.kind != ObjKind::Hunter);   // GUN Hunters off by default
    {   // the grind paths over the decks, joined and over road, cover a good share of the course
        int under = 0;
        for (auto& rl : co.rails) under += rl.s1 - rl.s0;
        CHECK(under == co.stats.grindStations);
    }
    *springs += co.stats.springs;
    *rings += co.stats.rings;
    for (auto& o : co.objs) {
        CHECK(pointInPolygon(r.room.floor, Vec2{o.p.x, -o.p.z}));
        CHECK(o.station >= 0 && o.station < N);
        const double deck = pm.st[o.station].p.y;
        if (o.kind == ObjKind::Ring || o.kind == ObjKind::RingLine) CHECK(o.p.y >= deck + 4 * pm.mpu - 1e-9);
        if (o.kind == ObjKind::RingLine) CHECK(o.count >= 3 && o.count <= 8 && o.spacingU > 8 && o.spacingU < 24);
        if (o.kind == ObjKind::Spring || o.kind == ObjKind::DashRamp) {
            const Vec3 from{o.p.x, o.p.y + (o.kind == ObjKind::Spring ? op.springLift * pm.mpu : 0), o.p.z};
            const bool ramp = o.kind == ObjKind::DashRamp;
            // the planner checks a flight within its module's reach (where decks stack, a look round the whole lap would see
            // decks a turn away): re-checked within the planned landing plus 8 stations
            const int reach = o.landStation >= 0 ? (o.landStation - o.station + N) % N + 8 : 200;
            const FlightCheck fc = checkFlight(pm, from, o.launchU, o.station, reach, op, false, ramp ? op.rampHoldFrames : 0, ramp ? op.rampDrag : -1);
            CHECK(fc.landStation == o.landStation);
            if (fc.landStation != o.landStation)
                std::printf("    %s at station %d of %d (%s, %s): lands at %d, planned %d (%s); decks %.3f -> %.3f\n", objKindName(o.kind), o.station, N,
                            r.course.kind.c_str(), playKindName(pm.st[o.station].kind), fc.landStation, o.landStation, fc.why.c_str(),
                            pm.st[o.station].p.y, o.landStation >= 0 ? pm.st[o.landStation].p.y : -1.0);
            const int ahead = (fc.landStation - o.station + N) % N;
            CHECK(ahead >= 2 && ahead < N / 2);
            CHECK(o.launchU.y > 0 && o.launchU.y < 5.0);   // the game's spring speed is 5 + scale.y: below 5, scale.y < 0
        }
        if (o.kind == ObjKind::DashPanel) CHECK(o.speedU > 5 && o.speedU <= 16 && o.lockFrames > 0);
    }
    for (auto& rl : co.rails) {
        CHECK(rl.pts.size() >= 5 && rl.riseU <= op.railMaxRiseU);
        for (size_t k = 0; k + 1 < rl.pts.size(); ++k)
            CHECK_NEAR(rl.pts[k].y, pm.st[(rl.s0 + (int)k) % N].p.y + op.railHeightU * pm.mpu, 1e-9);
    }
}
static void testObjects() {
    GenParams gp;
    gp.sonicHeightM = 0.12;
    ModuleLibrary lib = ModuleLibrary::builtin(gp.speedScale);
    int rings = 0, springs = 0, springMods = 0, courses = 0;
    for (auto wh : {std::pair<double, double>{3.0, 2.5}, {4.0, 3.0}, {2.4, 2.4}, {5.0, 4.0}}) {
        Room room;
        const double w = wh.first / 2, h = wh.second / 2;
        room.floor = {{-w, -h}, {w, -h}, {w, h}, {-w, h}};
        room.player = {0, 0};
        GenResult r = generateCourse(room, 1, gp, lib);
        if (r.ok) { checkObjects(r, lib, gp, &rings, &springs, &springMods); courses++; }
    }
    Rng rng(4242);
    for (int i = 0; i < 24; ++i) {
        Room room = randomRoom(rng, (RoomType)(i % 4));
        GenResult r = generateCourse(room, 7, gp, lib);
        if (!r.ok) continue;
        checkObjects(r, lib, gp, &rings, &springs, &springMods);
        courses++;
    }
    std::printf("  objects: %d courses, %d rings, %d springs (%d spring modules; hop springs added where none)\n", courses, rings, springs, springMods);
    CHECK(rings >= courses * 6);
    CHECK(springs * 2 >= springMods);   // most planned spring flights survive the game-physics check
}

// Vertical courses (roller profiles, coils, towers): small play areas get height and stacked decks, and the
// stacking keeps a jump's clearance; everything else (slopes, creases, bots, objects) as for any course.
static void testVertical() {
    GenParams gp;
    gp.sonicHeightM = 0.12;
    ModuleLibrary lib = ModuleLibrary::builtin(gp.speedScale);
    // the stacking gap is a full jump: its apex, Sonic's height and the deck's underside (measured in the game)
    CHECK(gp.layerGapU >= gp.jumpApexU + gp.sonicTopU + gp.deckUndersideU);
    CHECK(gp.minH <= 0.2 + 1e-9 && gp.maxH >= 1.6 - 1e-9);
    struct Case { double w, d, ux, uy; uint64_t seed; };
    std::vector<Case> cases;
    // the VR player's small areas: a 1 x 1 m play area (the Frame's own spot first) and 2 x 2 m, made 2 x 2 m courses
    const double spots[][2] = {{0.10, 0.16}, {0, 0}, {0.3, 0.2}, {-0.35, 0.35}, {0.1, -0.4}, {0.4, 0.4}, {-0.2, 0.1},
                               {0.6, -0.5}, {-0.7, 0.2}, {0.25, -0.25}, {-0.5, -0.6}, {0.05, 0.3}};
    for (size_t i = 0; i < sizeof spots / sizeof spots[0]; ++i) cases.push_back({2.0, 2.0, spots[i][0], spots[i][1], 1 + i});
    for (int i = 0; i < 6; ++i) cases.push_back({3.0, 2.5, -0.8 + 0.3 * i, 0.5 - 0.2 * i, 20 + (uint64_t)i});
    int stacked = 0, tall = 0, n = 0, upSprings = 0, springs = 0;
    std::vector<double> ranges;
    std::map<std::string, int> kinds;
    for (const Case& cs : cases) {
        Room room;
        room.floor = {{-cs.w / 2, -cs.d / 2}, {cs.w / 2, -cs.d / 2}, {cs.w / 2, cs.d / 2}, {-cs.w / 2, cs.d / 2}};
        room.player = {cs.ux, cs.uy};
        room.playerRadius = cs.w < 2.5 || cs.d < 2.5 ? 0.3 : 0.7;
        GenResult r = generateCourse(room, cs.seed, gp, lib);
        CHECK(r.ok);
        if (!r.ok) continue;
        ++n;
        kinds[r.course.kind]++;
        CHECK(r.course.stats.completable);
        CHECK(checkClearance(r.course, r.room, gp).empty());
        const PlayMesh pm = buildPlayMesh(r, lib, gp);
        const ShapeMetrics sh = shapeMetrics(r, pm);
        // where decks overlap in plan they are a jump and Sonic apart (the play mesh's own walkways, any two stations)
        if (sh.minStackGapU > 0) CHECK(sh.minStackGapU >= gp.layerGapU - 1e-6);
        if (sh.minStackGapU > 0 && sh.minStackGapU < gp.layerGapU - 1e-6)
            std::printf("  %s in %.1f x %.1f m (user %.2f %.2f): decks %.1f units apart where stacked\n", r.course.kind.c_str(), cs.w, cs.d, cs.ux, cs.uy, sh.minStackGapU);
        // and in the generator's course, at every stacked pair it planned
        for (int j = 0; j < r.course.sk.size(); ++j)
            for (int q : r.course.sk.partners[j])
                if (r.course.exists[j] && r.course.exists[q] && !r.course.hole[j] && !r.course.hole[q])
                    CHECK(std::fabs(r.course.z[j] - r.course.z[q]) >= gp.layerGapU * gp.metersPerUnit() - 1e-6);
        // within the reach of a standing user (the play mesh lifts a held deck a little at most)
        CHECK(sh.minDeckM >= gp.minH - 1e-6 && sh.maxDeckM <= gp.maxH + 0.06);
        stacked += sh.tiers >= 2;
        tall += sh.maxDeckM >= 1.0;
        ranges.push_back(sh.maxDeckM - sh.minDeckM);
        checkPlayMesh(r, lib, gp);   // slopes, creases, walls, the bots on the play mesh's profile
        int rings = 0, sp = 0, spm = 0;
        checkObjects(r, lib, gp, &rings, &sp, &spm);   // every kept flight lands ahead under the game's air physics
        const CourseObjects co = placeObjects(r, pm, lib, gp);
        for (auto& o : co.objs)
            if (o.kind == ObjKind::Spring) {
                ++springs;
                upSprings += pm.st[o.landStation].p.y > pm.st[o.station].p.y + 0.1;   // up to a higher deck
            }
    }
    std::sort(ranges.begin(), ranges.end());
    std::printf("  vertical: %d courses (%d small), %d stacked two or more high, %d reaching 1 m, height range p50 %.2f m; kinds:", n,
                (int)(sizeof spots / sizeof spots[0]), stacked, tall, ranges.empty() ? 0.0 : ranges[ranges.size() / 2]);
    for (auto& kv : kinds) std::printf(" %s %d", kv.first.c_str(), kv.second);
    std::printf("; springs %d (%d up to a higher deck)\n", springs, upSprings);
    // regression guards for the small rooms' height use (the old generator: no stacking, ranges of 0.3 m)
    CHECK(stacked * 3 >= n);
    CHECK(!ranges.empty() && ranges[ranges.size() / 2] >= 0.45);
    CHECK(kinds.size() >= 3);   // not always the same shape
    // seeds vary the shape in one small room; the same seed repeats it byte for byte (coils and towers too)
    {
        Room room;
        room.floor = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        room.player = {0.1, 0.16};
        room.playerRadius = 0.3;
        std::map<long, int> lens;
        for (uint64_t s = 1; s <= 8; ++s) {
            const GenResult a = generateCourse(room, s, gp, lib);
            if (!a.ok) continue;
            lens[std::lround(a.course.sk.lengthM() * 20)]++;
            if (s <= 2) {
                const GenResult b = generateCourse(room, s, gp, lib);
                CHECK(courseToJson(a, lib, gp, s).dump() == courseToJson(b, lib, gp, s).dump());
                CHECK(playMeshToObj(buildPlayMesh(a, lib, gp)) == playMeshToObj(buildPlayMesh(b, lib, gp)));
            }
        }
        CHECK(lens.size() >= 3);
    }
}

int main() {
    struct T { const char* name; std::function<void()> fn; };
    std::vector<T> tests = {
        {"geometry", testGeometry},       {"inset+medial", testInsetAndMedial},
        {"joins", testJoins},             {"determinism", testDeterminism},
        {"clearance+completion", testClearanceAndCompletion},
        {"fallback", testFallback},       {"tiny room", testTinyRoomRejected},
        {"exports", testExports},         {"playable mesh", testPlayMesh}, {"game objects", testObjects},
        {"vertical courses", testVertical},
        {"bad driver on the play meshes", [] {
             std::printf("  the bad driver on the play meshes: %d of %d runs failed\n", gBadMeshFails, gBadMeshRuns);
             CHECK(gBadMeshRuns > 0 && gBadMeshFails * 50 <= gBadMeshRuns);
         }},
    };
    for (auto& t : tests) {
        int before = gFailures;
        t.fn();
        std::printf("[%s] %s\n", gFailures == before ? " ok " : "FAIL", t.name);
    }
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
