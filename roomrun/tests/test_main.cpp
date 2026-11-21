// Minimal self-contained test runner (no framework dependency).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <functional>
#include <string>
#include <vector>

#include "roomrun/export.hpp"
#include "roomrun/field.hpp"
#include "roomrun/generator.hpp"
#include "roomrun/playmesh.hpp"

using namespace rr;

static int gFailures = 0, gChecks = 0;
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
    PlayMesh m = buildPlayMesh(r, lib, gp, pp);
    const int N = r.course.sk.size();
    CHECK((int)m.st.size() == N);
    const double rise = pp.maxRiseSlope * r.course.sk.stepM + 1e-6;
    int steep = 0;
    for (int j = 0; j < N; ++j) steep += m.st[(j + 1) % N].p.y - m.st[j].p.y > rise;
    CHECK(steep == 0);
    int stations = 0, badDeck = 0, badWall = 0, outside = 0;
    for (auto& c : m.chunks) {
        CHECK(c.s1 - c.s0 <= pp.chunkStations && c.s1 > c.s0);
        stations += c.s1 - c.s0;
        CHECK(c.tri.size() == (size_t)(c.s1 - c.s0) * 8 * 9);   // per station: the deck, its underside, two walls
        for (size_t t = 0; t < c.tri.size() / 9; ++t) {
            const float* v = &c.tri[9 * t];
            const Vec3 a{v[0], v[1], v[2]}, b{v[3], v[4], v[5]}, cc{v[6], v[7], v[8]};
            const Vec3 e1 = b - a, e2 = cc - a;
            Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
            const double l = n.len();
            if (l < 1e-12) continue;
            const double ny = n.y / l;
            const int j = c.s0 + (int)(t / 8), jn = (j + 1) % N;
            const bool drop = m.st[jn].p.y < m.st[j].p.y - 1e-6;   // a drop module's lip: steep, down only
            if ((t % 8) < 2) badDeck += ny <= 0.0 || (ny < 0.5 && !drop);   // the deck faces up
            else if ((t % 8) < 4) badDeck += ny >= 0.0 || (ny > -0.5 && !drop);   // its underside faces down (a ceiling)
            else badWall += std::fabs(ny) > 1e-3;         // the walls stand upright
            for (int k = 0; k < 3; ++k) outside += !pointInPolygon(r.room.floor, Vec2{v[3 * k], -v[3 * k + 2]});
        }
    }
    CHECK(stations == N);
    CHECK(badDeck == 0);
    CHECK(badWall == 0);
    CHECK(outside == 0);
    bool road = false, curb = false;
    for (auto& g : m.groups) { road |= g.name == "road" && g.triangles() > 0; curb |= g.name == "curb_a" && g.triangles() > 0; }
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

int main() {
    struct T { const char* name; std::function<void()> fn; };
    std::vector<T> tests = {
        {"geometry", testGeometry},       {"inset+medial", testInsetAndMedial},
        {"joins", testJoins},             {"determinism", testDeterminism},
        {"clearance+completion", testClearanceAndCompletion},
        {"fallback", testFallback},       {"tiny room", testTinyRoomRejected},
        {"exports", testExports},         {"playable mesh", testPlayMesh},
    };
    for (auto& t : tests) {
        int before = gFailures;
        t.fn();
        std::printf("[%s] %s\n", gFailures == before ? " ok " : "FAIL", t.name);
    }
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
