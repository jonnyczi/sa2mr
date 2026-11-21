#include "roomrun/playmesh.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace rr {

const char* playKindName(PlayKind k) {
    switch (k) {
        case PlayKind::Road: return "road";
        case PlayKind::Ramp: return "ramp";
        case PlayKind::Rail: return "rail";
        case PlayKind::Dash: return "dash";
        case PlayKind::Spring: return "spring";
        case PlayKind::Bridge: return "bridge";
        case PlayKind::Loop: return "loop";
        case PlayKind::Start: return "start";
    }
    return "?";
}

size_t PlayMesh::collisionTriangles() const {
    size_t n = 0;
    for (auto& c : chunks) n += c.tri.size() / 9;
    return n;
}
size_t PlayMesh::drawTriangles() const {
    size_t n = 0;
    for (auto& g : groups) n += g.triangles();
    return n;
}

namespace {

// the plan's (x, y, z up) to the Y-up frame
Vec3 Y(const Vec3& v) { return {v.x, v.z, -v.y}; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

enum G { gRoad, gRamp, gRail, gDash, gSpring, gBridge, gLoop, gStartA, gStartB, gEdge, gCurbA, gCurbB, gRailCurbA,
         gRailCurbB, gPillar, gCount };

struct Out {
    std::vector<PlayGroup> g;
    Out() {
        struct D { const char* n; float r, gg, b; };
        const D d[gCount] = {
            {"road", 1.00f, 0.42f, 0.05f},      {"ramp", 1.00f, 0.55f, 0.12f},   {"rail", 0.62f, 0.68f, 0.80f},
            {"dash", 0.10f, 0.80f, 0.95f},      {"spring", 0.92f, 0.16f, 0.20f}, {"bridge", 0.58f, 0.32f, 0.92f},
            {"loop", 0.20f, 0.80f, 0.32f},      {"start_a", 0.96f, 0.96f, 0.96f}, {"start_b", 0.08f, 0.08f, 0.10f},
            {"edge", 0.20f, 0.22f, 0.32f},      {"curb_a", 0.92f, 0.10f, 0.12f}, {"curb_b", 0.96f, 0.96f, 0.96f},
            {"rail_curb_a", 1.00f, 0.84f, 0.10f}, {"rail_curb_b", 0.12f, 0.12f, 0.14f}, {"pillar", 0.82f, 0.84f, 0.88f}};
        g.resize(gCount);
        for (int i = 0; i < gCount; ++i) { g[i].name = d[i].n; g[i].rgb[0] = d[i].r; g[i].rgb[1] = d[i].gg; g[i].rgb[2] = d[i].b; }
    }
    // plan-frame corners, counter-clockwise seen from the side the face shows
    void tri(int gi, const Vec3& a, const Vec3& b, const Vec3& c) {
        const Vec3 A = Y(a), B = Y(b), C = Y(c);
        Vec3 n = cross(B - A, C - A);
        const double l = n.len();
        if (l < 1e-14) return;
        n = n * (1.0 / l);
        PlayGroup& p = g[gi];
        for (const Vec3* v : {&A, &B, &C}) { p.pos.push_back((float)v->x); p.pos.push_back((float)v->y); p.pos.push_back((float)v->z); }
        p.nrm.push_back((float)n.x); p.nrm.push_back((float)n.y); p.nrm.push_back((float)n.z);
    }
    void quad(int gi, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) { tri(gi, a, b, c); tri(gi, a, c, d); }
    // an axis-aligned box in the plan frame (centre, half sizes), its faces outward
    void box(int gi, Vec3 c, double hx, double hy, double hz) {
        auto P = [&](int sx, int sy, int sz) { return Vec3{c.x + sx * hx, c.y + sy * hy, c.z + sz * hz}; };
        quad(gi, P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1));        // top (+z)
        quad(gi, P(-1, 1, -1), P(1, 1, -1), P(1, -1, -1), P(-1, -1, -1));    // bottom
        quad(gi, P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1));        // +x
        quad(gi, P(-1, 1, -1), P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1));    // -x
        quad(gi, P(1, 1, -1), P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1));        // +y
        quad(gi, P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1));    // -y
    }
};

void colTri(PlayChunk& ch, const Vec3& a, const Vec3& b, const Vec3& c) {
    for (const Vec3* v : {&a, &b, &c}) {
        const Vec3 w = Y(*v);
        ch.tri.push_back((float)w.x); ch.tri.push_back((float)w.y); ch.tri.push_back((float)w.z);
    }
}
void colQuad(PlayChunk& ch, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) { colTri(ch, a, b, c); colTri(ch, a, c, d); }

PlayKind kindOf(Kind k) {
    switch (k) {
        case Kind::Start: return PlayKind::Start;
        case Kind::Ramp: return PlayKind::Ramp;
        case Kind::Rail: return PlayKind::Rail;
        case Kind::Dash: return PlayKind::Dash;
        case Kind::Spring: return PlayKind::Spring;
        case Kind::Gap: case Kind::Homing: case Kind::Drop: return PlayKind::Bridge;
        case Kind::Loop: return PlayKind::Loop;
        default: return PlayKind::Road;
    }
}
int deckGroup(PlayKind k) {
    switch (k) {
        case PlayKind::Ramp: return gRamp;
        case PlayKind::Rail: return gRail;
        case PlayKind::Dash: return gDash;
        case PlayKind::Spring: return gSpring;
        case PlayKind::Bridge: return gBridge;
        case PlayKind::Loop: return gLoop;
        default: return gRoad;
    }
}

}  // namespace

PlayMesh buildPlayMesh(const GenResult& r, const ModuleLibrary& lib, const GenParams& gp, const PlayMeshParams& pp) {
    PlayMesh m;
    const Course& c = r.course;
    const int N = c.sk.size();
    const double mpu = gp.metersPerUnit();
    m.mpu = mpu;
    if (N < 3) return m;
    // the surface: the generator's where it has one, else a bridge between its two ends
    std::vector<double> z(N, 0.0);
    std::vector<uint8_t> valid(N, 0), bridged(N, 0);
    int any = -1;
    for (int j = 0; j < N; ++j) {
        valid[j] = c.exists[j] && !c.hole[j];
        if (valid[j]) { z[j] = c.z[j]; if (any < 0) any = j; }
    }
    if (any < 0) {
        for (int j = 0; j < N; ++j) { z[j] = gp.prefMinH; bridged[j] = 1; }
    } else {
        for (int k = 0; k < N;) {
            const int a = (any + k) % N;
            int len = 0;
            while (len < N && !valid[(a + 1 + len) % N]) ++len;
            if (len) {
                const int b = (a + 1 + len) % N;
                for (int i = 1; i <= len; ++i) {
                    const int j = (a + i) % N;
                    const double f = (double)i / (len + 1);
                    z[j] = z[a] + (z[b] - z[a]) * f;
                    bridged[j] = 1;
                }
            }
            k += len + 1;
        }
    }
    // per station: kind, half width, the plan frame
    std::vector<PlayKind> kind(N, PlayKind::Road);
    std::vector<double> hw(N);
    std::vector<Vec2> P(N), Nn(N), T(N);
    for (int j = 0; j < N; ++j) {
        const int mi = c.mod[j];
        if (mi >= 0 && mi < (int)c.mods.size()) kind[j] = kindOf(c.mods[mi].kind);
        if (c.rail[j]) kind[j] = PlayKind::Rail;
        hw[j] = (kind[j] == PlayKind::Rail ? pp.railHalfWidthU : 0.5 * gp.trackWidthU) * mpu;
        P[j] = c.sk.pts[j];
        T[j] = (c.sk.pts[(j + 1) % N] - c.sk.pts[(j + N - 1) % N]).norm();
        Nn[j] = T[j].perp();
    }
    // The walkway narrows gently where a road meets a rail strip (pp.widthRateU a station on each side):
    // a step from 12 to 7 units in one station made walls converging at 51 degrees, a corner Sonic stuck in.
    {
        const double rate = pp.widthRateU * mpu;
        for (int pass = 0; pass < N; ++pass) {
            bool changed = false;
            for (int j = 0; j < N; ++j) {
                const double lim = std::min(hw[(j + N - 1) % N], hw[(j + 1) % N]) + rate;
                if (hw[j] > lim + 1e-12) { hw[j] = lim; changed = true; }
            }
            if (!changed) break;
        }
    }
    // Each segment's effective run: the rise between two stations over it is the slope of the steepest
    // deck facet (lanes below). A facet with two corners on one station and one on the other rises dz over
    // the plan distance from the lone corner to the line through the pair, so the steepest facet's slope
    // is dz / run with run the shortest such distance. On a bend that is the inside (the same rise over a
    // shorter run: at the Frame's spring ramp the inner edge was 0.56 for 0.45 at the centreline, and its
    // full-width sliver triangles 34 degrees); where the width changes, the skewed lanes.
    // (fewer lanes on a very long course: the game caches every piece near Sonic in a pool of 8191 triangles)
    int lanes = std::max(1, pp.deckLanes);
    while (lanes > 1 && (size_t)N * (4 * lanes + 4) > (size_t)pp.maxCollisionTriangles) --lanes;
    m.lanes = lanes;
    std::vector<double> run(N);
    {
        auto lineDist = [](Vec2 p, Vec2 a, Vec2 b) {
            const Vec2 d = b - a;
            const double l = d.len();
            return l > 1e-12 ? std::fabs(d.x * (p.y - a.y) - d.y * (p.x - a.x)) / l : (p - a).len();
        };
        auto lp = [&](int j, int k) { return P[j] + Nn[j] * (hw[j] * (1.0 - 2.0 * k / lanes)); };
        for (int j = 0; j < N; ++j) {
            const int jn = (j + 1) % N;
            double rmin = c.sk.stepM;
            for (int k = 0; k < lanes; ++k) {
                const Vec2 A = lp(j, k), B = lp(j, k + 1), C = lp(jn, k + 1), D = lp(jn, k);
                rmin = std::min({rmin, lineDist(C, A, B), lineDist(A, C, D)});   // the facets (A B C) and (A C D)
            }
            run[j] = std::max(1e-4, rmin);
        }
    }
    // Drops (a downward step steeper than pp.dropSlope, a Drop module's lip) stay sharp: the smoothing
    // below never averages across one.
    std::vector<uint8_t> drop(N, 0);
    for (int j = 0; j < N; ++j) drop[j] = z[(j + 1) % N] - z[j] < -pp.dropSlope * run[j];
    // What the shaping keeps: decks the generator set on a furniture top stay on it (but for pp.boxSinkM where
    // they ease off its edge: held exactly, the edge was a crest of up to 35 degrees), and where one deck
    // passes over another the gap stays the generator's (or pp's layer gap, the smaller).
    const std::vector<double> z0 = z;
    std::vector<double> lo(N, -1e9);
    for (int j = 0; j < N; ++j)
        for (auto& b : r.room.boxes)
            if (b.rect.contains(P[j]) && z0[j] >= b.height - 1e-6) lo[j] = std::max(lo[j], b.height - pp.boxSinkM);
    struct Over { int up, low; double gap; };
    std::vector<Over> over;
    const double layerGap = gp.layerGapU * mpu;
    for (int j = 0; j < N && j < (int)c.sk.partners.size(); ++j)
        for (int q : c.sk.partners[j])
            if (z0[j] > z0[q] + 1e-6) over.push_back({j, q, std::min(z0[j] - z0[q], layerGap)});
    std::vector<uint8_t> raised(N, 0);
    // A step steeper than maxRiseSlope (on its shortest edge) becomes a ramp: upward spread backwards, and
    // downward (Sonic runs both ways: head-relative steering) spread forwards, except a drop. Both only raise.
    auto riseLimit = [&](bool count) {
        for (int pass = 0; pass < 4 * N; ++pass) {
            bool changed = false;
            for (int j = N - 1; j >= 0; --j) {
                const int jn = (j + 1) % N;
                const double rise = pp.maxRiseSlope * run[j];
                if (z[jn] - z[j] > rise + 1e-9) { z[j] = z[jn] - rise; if (count) raised[j] = 1; changed = true; }
            }
            for (int j = 0; j < N; ++j) {
                const int jn = (j + 1) % N;
                const double fall = pp.maxFallSlope * run[j];
                if (!drop[j] && z[j] - z[jn] > fall + 1e-9) { z[jn] = z[j] - fall; if (count) raised[jn] = 1; changed = true; }
            }
            if (!changed) break;
        }
    };
    auto keep = [&]() {
        for (int j = 0; j < N; ++j) z[j] = std::max(z[j], lo[j]);
        for (const Over& o : over)
            if (z[o.up] - z[o.low] < o.gap) z[o.up] = z[o.low] + o.gap;
    };
    riseLimit(true);
    // The vertical profile smoothed: a box filter over 2 h + 1 stations, pp.smoothPasses times (about a
    // Gaussian), so slopes change gradually instead of in one station. A crease of 24 degrees in one
    // station (the top of a spring ramp) threw Sonic off as a ledge; a sag stopped him as a step. A box
    // filter keeps a straight ramp straight and a plateau level, and never makes a slope steeper than the
    // steepest it averages; the drops above bound its windows.
    {
        const int h = std::max(0, pp.smoothHalfStations);
        std::vector<double> zn(N);
        for (int pass = 0; pass < pp.smoothPasses && h > 0; ++pass) {
            for (int j = 0; j < N; ++j) {
                double s = z[j];
                int n = 1;
                for (int k = 1; k <= h; ++k) { const int i = (j - k + N) % N; if (drop[i]) break; s += z[i]; ++n; }
                for (int k = 1; k <= h; ++k) { if (drop[(j + k - 1) % N]) break; s += z[(j + k) % N]; ++n; }
                zn[j] = s / n;
            }
            z = zn;
            keep();
            riseLimit(false);
        }
        // where the limits cut back in (a valley's ramps raised to the limit meet in a V), smoothed locally
        // again, until the slope changes by at most pp.maxCreaseSlope a station or nothing more gives (a crest
        // a limit holds up, a deck over a crossing, stays as sharp as the limit makes it)
        const double stepM = c.sk.stepM;
        for (int it = 0; it < (pp.smoothPasses > 0 ? 200 : 0); ++it) {
            const std::vector<double> before = z;
            for (int j = 0; j < N; ++j) {
                const int jp = (j + N - 1) % N, jn = (j + 1) % N;
                if (drop[jp] || drop[j]) { zn[j] = z[j]; continue; }
                const double cr = ((z[jn] - z[j]) - (z[j] - z[jp])) / stepM;
                zn[j] = std::fabs(cr) > pp.maxCreaseSlope ? (z[jp] + 2 * z[j] + z[jn]) / 4 : z[j];
            }
            z = zn;
            for (int k = 0; k < 16; ++k) {   // until both hold (each only raises)
                const std::vector<double> b2 = z;
                keep();
                riseLimit(false);
                if (z == b2) break;
            }
            if (z == before) break;
        }
    }
    for (int j = 0; j < N; ++j) m.raisedStations += raised[j];
    for (int j = 0; j < N; ++j) {
        m.bridgedStations += bridged[j];
        m.railStations += kind[j] == PlayKind::Rail;
        PlayStation s;
        s.p = Y(Vec3{P[j].x, P[j].y, z[j]});
        s.fwd = Y(Vec3{T[j].x, T[j].y, 0});
        s.halfW = hw[j];
        s.kind = kind[j];
        s.bridged = bridged[j] != 0;
        s.held = z[j] <= lo[j] + 1e-9 ? 1 : 0;
        for (const Over& o : over) if (o.up == j && z[j] - z[o.low] <= o.gap + 1e-9) s.held = 2;
        m.st.push_back(s);
    }
    auto at = [&](int j, double side, double up) {   // side + to the left (metres from the centreline), up from the deck
        return Vec3{P[j].x + Nn[j].x * side, P[j].y + Nn[j].y * side, z[j] + up};
    };
    // The deck across its width in pp.deckLanes lanes, the same fractions of the half width at every
    // station, so neighbouring segments (and modules of other widths) share their edge vertices exactly.
    // A bend's ramp is a twisted surface (its inner edge steeper than its outer): one quad across the full
    // width folded along its diagonal into two facets 10 degrees apart, and Sonic lost the ground there.
    auto lane = [&](int j, int k, double up = 0) { return at(j, hw[j] * (1.0 - 2.0 * k / lanes), up); };   // k = 0 the left edge
    // collision: the deck, its underside and both walls, chunked. The underside (facing down, the deck's
    // thickness below it) is a ceiling: without it the game let Sonic through a deck from below and set
    // him on top of it (a jump under a bridge came out on the bridge; the Frame, 2026-11-21)
    const double wallH = pp.wallHeightU * mpu, under = pp.undersideU * mpu;
    const int cs = std::max(4, pp.chunkStations);
    // The triangles of a piece in order of their middle's Z, highest first. The game's collision cache
    // (0x0048B610 via 0x004974F0) files each triangle at the head of a 16-unit band of Z, so a piece's
    // chain runs in our reverse order inside a band, and its query (0x0048BAF0) walks that chain and stops
    // at the first triangle past its Z window, as if the chain were sorted. Highest first makes it sorted.
    auto finish = [&](PlayChunk&& ch) {
        if (pp.sortForGame) {
            const size_t nt = ch.tri.size() / 9;
            std::vector<size_t> ord(nt);
            for (size_t t = 0; t < nt; ++t) ord[t] = t;
            auto midZ = [&](size_t t) { return ch.tri[9 * t + 2] + ch.tri[9 * t + 5] + ch.tri[9 * t + 8]; };
            std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) { return midZ(a) > midZ(b); });
            std::vector<float> sorted;
            sorted.reserve(ch.tri.size());
            for (size_t t : ord) sorted.insert(sorted.end(), ch.tri.begin() + 9 * t, ch.tri.begin() + 9 * t + 9);
            ch.tri.swap(sorted);
        }
        if (!ch.tri.empty()) m.chunks.push_back(std::move(ch));
    };
    for (int s0 = 0; s0 < N; s0 += cs) {
        PlayChunk ch, cw;
        ch.s0 = cw.s0 = s0;
        ch.s1 = cw.s1 = std::min(N, s0 + cs);
        cw.walls = true;
        for (int j = ch.s0; j < ch.s1; ++j) {
            const int jn = (j + 1) % N;
            const Vec3 L0h = at(j, hw[j], wallH), R0h = at(j, -hw[j], wallH), L1h = at(jn, hw[jn], wallH), R1h = at(jn, -hw[jn], wallH);
            for (int k = 0; k < lanes; ++k)    // the deck, up, lane by lane from the left
                colQuad(ch, lane(j, k), lane(j, k + 1), lane(jn, k + 1), lane(jn, k));
            for (int k = 0; k < lanes; ++k) {   // its underside, down: the deck's facets turned over (parallel to them)
                const Vec3 A = lane(j, k, -under), B = lane(j, k + 1, -under), C = lane(jn, k + 1, -under), D = lane(jn, k, -under);
                colTri(ch, A, C, B);
                colTri(ch, A, D, C);
            }
            {   // the walls, facing in
                PlayChunk& w = pp.wallsApart ? cw : ch;
                colQuad(w, at(j, hw[j], 0), at(jn, hw[jn], 0), L1h, L0h);     // the left wall
                colQuad(w, at(jn, -hw[jn], 0), at(j, -hw[j], 0), R0h, R1h);   // the right wall
            }
        }
        finish(std::move(ch));
        finish(std::move(cw));
    }
    // drawing
    Out o;
    const double thick = pp.deckThickU * mpu, ch = pp.curbHeightU * mpu, cw = pp.curbWidthU * mpu;
    for (int j = 0; j < N; ++j) {
        const int jn = (j + 1) % N;
        const Vec3 L0 = at(j, hw[j], 0), R0 = at(j, -hw[j], 0), L1 = at(jn, hw[jn], 0), R1 = at(jn, -hw[jn], 0);
        for (int k = 0; k < lanes; ++k) {   // the drawn deck is the collision's, lane by lane
            int g = deckGroup(kind[j]);
            if (kind[j] == PlayKind::Start) g = ((j + (2 * k < lanes ? 0 : 1)) & 1) ? gStartA : gStartB;   // checkered halves
            o.quad(g, lane(j, k), lane(j, k + 1), lane(jn, k + 1), lane(jn, k));
        }
        const Vec3 L0d = at(j, hw[j], -thick), R0d = at(j, -hw[j], -thick), L1d = at(jn, hw[jn], -thick), R1d = at(jn, -hw[jn], -thick);
        o.quad(gEdge, L1d, R1d, R0d, L0d);   // underside
        o.quad(gEdge, L1d, L0d, L0, L1);     // the left side, outward
        o.quad(gEdge, R0d, R1d, R1, R0);     // the right side, outward
        // the curbs: striped every two stations
        const bool rail = kind[j] == PlayKind::Rail;
        const int ca = rail ? gRailCurbA : gCurbA, cb = rail ? gRailCurbB : gCurbB;
        const int cg = ((j / 2) & 1) ? cb : ca;
        for (int side = 0; side < 2; ++side) {
            const double sg = side ? -1.0 : 1.0;
            const Vec3 o0 = at(j, sg * hw[j], 0), o1 = at(jn, sg * hw[jn], 0);
            const Vec3 i0 = at(j, sg * (hw[j] - cw), 0), i1 = at(jn, sg * (hw[jn] - cw), 0);
            const Vec3 o0h = at(j, sg * hw[j], ch), o1h = at(jn, sg * hw[jn], ch);
            const Vec3 i0h = at(j, sg * (hw[j] - cw), ch), i1h = at(jn, sg * (hw[jn] - cw), ch);
            if (!side) {
                o.quad(cg, o0h, i0h, i1h, o1h);   // top
                o.quad(cg, i0, i1, i1h, i0h);     // inner face (toward the middle)
                o.quad(cg, o1, o0, o0h, o1h);     // outer face
            } else {
                o.quad(cg, i0h, o0h, o1h, i1h);
                o.quad(cg, i1, i0, i0h, i1h);
                o.quad(cg, o0, o1, o1h, o0h);
            }
        }
    }
    // pillars: down to the floor or a furniture top; none through a deck below
    const int every = std::max(2, (int)std::lround(pp.pillarEveryM / c.sk.stepM));
    const double ph = pp.pillarHalfU * mpu;
    for (int j = 0; j < N; j += every) {
        bool deckBelow = false;
        if (j < (int)c.sk.partners.size())
            for (int q : c.sk.partners[j]) deckBelow |= z[q] < z[j] - 1e-6;
        if (deckBelow) continue;
        double base = 0;
        for (auto& b : r.room.boxes) if (b.rect.contains(P[j])) base = std::max(base, b.height);
        const double top = z[j] - thick;
        if (top - base < 0.02) continue;
        o.box(gPillar, Vec3{P[j].x, P[j].y, (top + base) / 2}, ph, ph, (top - base) / 2);
    }
    for (auto& g : o.g)
        if (!g.pos.empty()) m.groups.push_back(std::move(g));
    (void)lib;
    return m;
}

std::string playMeshToObj(const PlayMesh& m) {
    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(4);
    o << "# Room Run playable mesh (" << kGeneratorVersion << "), metres, Y-up\n";
    int nv = 0;
    for (auto& g : m.groups) {
        o << "o " << g.name << "\n";
        for (size_t t = 0; t < g.triangles(); ++t) {
            for (int k = 0; k < 3; ++k) o << "v " << g.pos[9 * t + 3 * k] << " " << g.pos[9 * t + 3 * k + 1] << " " << g.pos[9 * t + 3 * k + 2] << "\n";
            o << "f " << nv + 1 << " " << nv + 2 << " " << nv + 3 << "\n";
            nv += 3;
        }
    }
    o << "o collision\n";
    for (auto& c : m.chunks)
        for (size_t t = 0; t < c.tri.size() / 9; ++t) {
            for (int k = 0; k < 3; ++k) o << "v " << c.tri[9 * t + 3 * k] << " " << c.tri[9 * t + 3 * k + 1] << " " << c.tri[9 * t + 3 * k + 2] << "\n";
            o << "f " << nv + 1 << " " << nv + 2 << " " << nv + 3 << "\n";
            nv += 3;
        }
    return o.str();
}

}  // namespace rr
