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
    // an upward step steeper than maxRiseSlope is spread backwards into a ramp
    {
        const double rise = pp.maxRiseSlope * c.sk.stepM;
        std::vector<uint8_t> raised(N, 0);
        for (int pass = 0; pass < 4 * N; ++pass) {
            bool changed = false;
            for (int j = N - 1; j >= 0; --j) {
                const int jn = (j + 1) % N;
                if (z[jn] - z[j] > rise + 1e-9) { z[j] = z[jn] - rise; raised[j] = 1; changed = true; }
            }
            if (!changed) break;
        }
        for (int j = 0; j < N; ++j) m.raisedStations += raised[j];
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
        m.bridgedStations += bridged[j];
        m.railStations += kind[j] == PlayKind::Rail;
        PlayStation s;
        s.p = Y(Vec3{P[j].x, P[j].y, z[j]});
        s.fwd = Y(Vec3{T[j].x, T[j].y, 0});
        s.halfW = hw[j];
        s.kind = kind[j];
        s.bridged = bridged[j] != 0;
        m.st.push_back(s);
    }
    auto at = [&](int j, double side, double up) {   // side + to the left (metres from the centreline), up from the deck
        return Vec3{P[j].x + Nn[j].x * side, P[j].y + Nn[j].y * side, z[j] + up};
    };
    // collision: the deck and both walls, chunked
    const double wallH = pp.wallHeightU * mpu;
    const int cs = std::max(4, pp.chunkStations);
    for (int s0 = 0; s0 < N; s0 += cs) {
        PlayChunk ch;
        ch.s0 = s0;
        ch.s1 = std::min(N, s0 + cs);
        for (int j = ch.s0; j < ch.s1; ++j) {
            const int jn = (j + 1) % N;
            const Vec3 L0 = at(j, hw[j], 0), R0 = at(j, -hw[j], 0), L1 = at(jn, hw[jn], 0), R1 = at(jn, -hw[jn], 0);
            const Vec3 L0h = at(j, hw[j], wallH), R0h = at(j, -hw[j], wallH), L1h = at(jn, hw[jn], wallH), R1h = at(jn, -hw[jn], wallH);
            colQuad(ch, L0, R0, R1, L1);       // the deck, up
            colQuad(ch, L0, L1, L1h, L0h);     // the left wall, facing in
            colQuad(ch, R1, R0, R0h, R1h);     // the right wall, facing in
        }
        m.chunks.push_back(std::move(ch));
    }
    // drawing
    Out o;
    const double thick = pp.deckThickU * mpu, ch = pp.curbHeightU * mpu, cw = pp.curbWidthU * mpu;
    for (int j = 0; j < N; ++j) {
        const int jn = (j + 1) % N;
        const Vec3 L0 = at(j, hw[j], 0), R0 = at(j, -hw[j], 0), L1 = at(jn, hw[jn], 0), R1 = at(jn, -hw[jn], 0);
        if (kind[j] == PlayKind::Start) {   // a checkered start: two halves, alternating
            const Vec3 M0 = at(j, 0, 0), M1 = at(jn, 0, 0);
            const bool odd = j & 1;
            o.quad(odd ? gStartA : gStartB, L0, M0, M1, L1);
            o.quad(odd ? gStartB : gStartA, M0, R0, R1, M1);
        } else {
            o.quad(deckGroup(kind[j]), L0, R0, R1, L1);
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
