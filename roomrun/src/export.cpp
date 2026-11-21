#include "roomrun/export.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace rr {

// ---------------------------------------------------------------------------- JSON

static Json envJson(const Envelope& e) {
    Json j = Json::object();
    j.set("vMin", e.vMin); j.set("vMax", e.vMax); j.set("lat", e.lat); j.set("head", e.head);
    return j;
}

Json courseToJson(const GenResult& r, const ModuleLibrary& lib, const GenParams& gp, uint64_t userSeed) {
    const Course& c = r.course;
    const double mpu = gp.metersPerUnit();
    Json j = Json::object();
    j.set("generator", kGeneratorVersion);
    j.set("library", lib.name);
    char hex[32];
    std::snprintf(hex, sizeof hex, "%016llx", (unsigned long long)c.seed);
    j.set("courseSeed", hex);
    j.set("userSeed", (double)userSeed);
    j.set("skeleton", c.kind);
    j.set("fallback", c.fallback);
    j.set("repairs", c.repairs);
    Json sc = Json::object();
    sc.set("sonicHeightM", gp.sonicHeightM);
    sc.set("metersPerUnit", mpu);
    sc.set("speedScale", gp.speedScale);
    sc.set("trackWidthM", gp.trackWidthU * mpu);
    sc.set("stationStepM", c.sk.stepM);
    j.set("scale", sc);
    j.set("lengthM", c.sk.lengthM());
    j.set("room", roomToJson(r.room));
    Json mods = Json::array();
    for (auto& m : c.mods) {
        const ModuleDef& d = lib.defs[m.def];
        Json o = Json::object();
        o.set("id", d.id);
        o.set("kind", kindName(m.kind));
        o.set("class", className(d.cls));
        o.set("s0", m.s0); o.set("s1", m.s1);
        o.set("startM", m.s0 * c.sk.stepM);
        o.set("lengthM", m.n() * c.sk.stepM);
        o.set("h0", m.h0); o.set("h1", m.h1);
        if (m.lipN) o.set("lipN", m.lipN);
        if (m.holeN) o.set("holeN", m.holeN);
        if (m.padN) o.set("padN", m.padN);
        o.set("entry", envJson(m.in));
        o.set("exit", envJson(m.out));
        if (m.repaired) o.set("repaired", true);
        mods.push(o);
    }
    j.set("modules", mods);
    Json st = Json::object();
    Json xs = Json::array(), ys = Json::array(), zs = Json::array(), fl = Json::array(), mi = Json::array();
    for (int i = 0; i < c.sk.size(); ++i) {
        xs.push(c.sk.pts[i].x); ys.push(c.sk.pts[i].y); zs.push(c.z[i]);
        fl.push((int)(c.exists[i] | (c.walled[i] << 1) | (c.rail[i] << 2) | (c.hole[i] << 3)));
        mi.push(c.mod[i]);
    }
    st.set("flagsLegend", "1=surface 2=walled 4=rail 8=net-gap");
    st.set("x", xs); st.set("y", ys); st.set("z", zs); st.set("flags", fl); st.set("module", mi);
    j.set("stations", st);
    Json rings = Json::array();
    for (auto& p : c.rings) { Json v = Json::array(); v.push(p.x); v.push(p.y); v.push(p.z); rings.push(v); }
    j.set("rings", rings);
    Json sk = Json::array();
    for (auto& s : c.skill) {
        Json o = Json::object();
        o.set("s0", s.s0); o.set("s1", s.s1); o.set("zM", s.zM);
        sk.push(o);
    }
    j.set("skillRoutes", sk);
    Json rk = Json::object();
    rk.set("laps", 3); rk.set("A", c.rankA); rk.set("B", c.rankB); rk.set("C", c.rankC);
    j.set("rankSeconds", rk);
    Json v = Json::object();
    v.set("runs", c.stats.runs);
    v.set("failedRuns", c.stats.failedRuns);
    v.set("completable", c.stats.completable);
    v.set("expertLapS", c.stats.expertLap);
    v.set("averageLapS", c.stats.averageLap);
    v.set("badLapS", c.stats.badLap);
    v.set("netCatches", c.stats.netCatches);
    v.set("fallRestarts", c.stats.falls);
    j.set("validation", v);
    return j;
}

// ---------------------------------------------------------------------------- SVG

static const char* kindColor(Kind k) {
    switch (k) {
        case Kind::Start: return "#222222";
        case Kind::Track: return "#8592a0";
        case Kind::Ramp: return "#5b7fa8";
        case Kind::Dash: return "#f2c200";
        case Kind::Spring: return "#e63946";
        case Kind::Drop: return "#f4a261";
        case Kind::Gap: return "#ff7f11";
        case Kind::Rail: return "#00b4d8";
        case Kind::Loop: return "#9b5de5";
        case Kind::Homing: return "#2a9d8f";
        case Kind::SkillRail: return "#b5179e";
        default: return "#000";
    }
}

std::string courseToSvg(const GenResult& r, const ModuleLibrary& lib, const GenParams& gp) {
    const Course& c = r.course;
    const Room& room = r.room;
    const double mpu = gp.metersPerUnit();
    const int N = c.sk.size();
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    for (auto& p : room.floor) {
        minx = std::min(minx, p.x); miny = std::min(miny, p.y); maxx = std::max(maxx, p.x); maxy = std::max(maxy, p.y);
    }
    const double W = 900, margin = 30;
    double s = std::min((W - 2 * margin) / (maxx - minx), 640 / (maxy - miny));
    double topH = (maxy - miny) * s + 2 * margin + 40;
    double profH = 240;
    double H = topH + profH + 70;
    auto X = [&](double x) { return margin + (x - minx) * s; };
    auto Y = [&](double y) { return 40 + margin + (maxy - y) * s; };
    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(1);
    o << "<svg xmlns='http://www.w3.org/2000/svg' width='" << W << "' height='" << H << "' viewBox='0 0 " << W << " " << H
      << "' font-family='Helvetica,Arial,sans-serif'>\n";
    o << "<rect width='100%' height='100%' fill='#ffffff'/>\n";
    // header
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "Room Run - %s%s | lap %.1f m | expert lap %.1f s, avg %.1f s | rank times (3 laps) A %.1f s, B %.1f s, C %.1f s | %d modules, %d repairs | Sonic %.0f cm",
                  c.kind.c_str(), c.fallback ? " (FALLBACK)" : "", c.sk.lengthM(), c.stats.expertLap, c.stats.averageLap,
                  c.rankA, c.rankB, c.rankC, (int)c.mods.size(), c.repairs, gp.sonicHeightM * 100);
    o << "<text x='" << margin << "' y='24' font-size='13' fill='#222'>" << buf << "</text>\n";
    // floor
    o << "<polygon points='";
    for (auto& p : room.floor) o << X(p.x) << "," << Y(p.y) << " ";
    o << "' fill='#f3efe6' stroke='#333' stroke-width='2'/>\n";
    // medial axis (subsampled)
    const Grid& F = r.free.clearance;
    for (int jj = 0; jj < F.ny; jj += 2)
        for (int ii = 0; ii < F.nx; ii += 2)
            if (r.free.ridge[(size_t)jj * F.nx + ii]) {
                Vec2 p = F.pos(ii, jj);
                o << "<rect x='" << X(p.x) - 0.8 << "' y='" << Y(p.y) - 0.8 << "' width='1.6' height='1.6' fill='#c3cfdd'/>";
            }
    o << "\n";
    // furniture
    for (auto& b : room.boxes) {
        bool tall = b.height > gp.tallBoxM;
        o << "<polygon points='";
        for (auto& p : b.rect.corners()) o << X(p.x) << "," << Y(p.y) << " ";
        o << "' fill='" << (tall ? "#9a9a9a" : "#d8c3a0") << "' stroke='#6b5a40' stroke-width='1.5' fill-opacity='0.85'/>\n";
        std::snprintf(buf, sizeof buf, "%s %.2fm%s", b.label.c_str(), b.height, tall ? " (obstacle)" : "");
        o << "<text x='" << X(b.rect.c.x) << "' y='" << Y(b.rect.c.y) + 4 << "' font-size='11' text-anchor='middle' fill='#4a3b22'>" << buf << "</text>\n";
    }
    // player cylinder
    o << "<circle cx='" << X(room.player.x) << "' cy='" << Y(room.player.y) << "' r='" << room.playerRadius * s
      << "' fill='#e6394622' stroke='#e63946' stroke-dasharray='6,4' stroke-width='1.5'/>\n";
    o << "<text x='" << X(room.player.x) << "' y='" << Y(room.player.y) + 4 << "' font-size='12' text-anchor='middle' fill='#c1121f'>YOU</text>\n";
    // track modules sorted by height (upper decks drawn last)
    double widthPx = gp.trackWidthU * mpu * s;
    std::vector<int> order(c.mods.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    auto meanZ = [&](int mi) { double z = 0; for (int j = c.mods[mi].s0; j < c.mods[mi].s1; ++j) z += c.z[j]; return z / c.mods[mi].n(); };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return meanZ(a) < meanZ(b); });
    for (int mi : order) {
        const Placed& m = c.mods[mi];
        const char* col = kindColor(m.kind);
        // contiguous runs of surface stations
        int j = m.s0;
        while (j < m.s1) {
            bool surf = c.exists[j];
            int k = j;
            while (k < m.s1 && (bool)c.exists[k] == surf) ++k;
            std::ostringstream pts;
            pts.setf(std::ios::fixed); pts.precision(1);
            int last = k == m.s1 ? k : k - 1;  // join to the next module's first station
            for (int q = j; q <= last; ++q) {
                const Vec2& p = c.sk.pts[q % N];
                pts << X(p.x) << "," << Y(p.y) << " ";
            }
            if (surf && !c.rail[j]) {
                o << "<polyline points='" << pts.str() << "' fill='none' stroke='#1b1b1b' stroke-width='" << widthPx + 3
                  << "' stroke-linejoin='round'/>\n";
                o << "<polyline points='" << pts.str() << "' fill='none' stroke='" << col << "' stroke-width='" << widthPx
                  << "' stroke-linejoin='round'/>\n";
            } else if (surf) {
                o << "<polyline points='" << pts.str() << "' fill='none' stroke='#03506f' stroke-width='" << std::max(3.0, widthPx * 0.35) + 2
                  << "'/>\n";
                o << "<polyline points='" << pts.str() << "' fill='none' stroke='" << col << "' stroke-width='" << std::max(3.0, widthPx * 0.35)
                  << "'/>\n";
            } else {
                o << "<polyline points='" << pts.str() << "' fill='none' stroke='" << col << "' stroke-width='2.5' stroke-dasharray='5,4'/>\n";
            }
            j = k;
        }
        // set-piece label
        if (m.kind != Kind::Track && m.kind != Kind::Ramp && m.kind != Kind::Rail) {
            int mid = (m.s0 + m.s1) / 2;
            Vec2 t = (c.sk.pts[(mid + 1) % N] - c.sk.pts[(mid + N - 1) % N]).norm();
            Vec2 p = c.sk.pts[mid] + t.perp() * (gp.trackWidthU * mpu * 1.3);
            const char* lab = m.kind == Kind::Start ? "START" : m.kind == Kind::Dash ? (lib.defs[m.def].id == "accel_pad" ? "accel" : "DASH")
                            : m.kind == Kind::Spring ? "SPRING" : m.kind == Kind::Drop ? "DROP" : m.kind == Kind::Gap ? "GAP"
                            : m.kind == Kind::Loop ? "LOOP" : m.kind == Kind::Homing ? "HOMING" : "";
            o << "<text x='" << X(p.x) << "' y='" << Y(p.y) + 3 << "' font-size='10' font-weight='bold' text-anchor='middle' fill='" << col
              << "' stroke='#fff' stroke-width='2.5' paint-order='stroke'>" << lab << "</text>\n";
            if (m.kind == Kind::Loop) {
                o << "<circle cx='" << X(c.sk.pts[mid].x) << "' cy='" << Y(c.sk.pts[mid].y) << "' r='" << std::max(4.0, widthPx * 0.6)
                  << "' fill='none' stroke='" << col << "' stroke-width='3'/>\n";
            }
        }
    }
    // skill routes
    for (auto& sr : c.skill) {
        o << "<polyline points='";
        for (int q = sr.s0; q < sr.s1; ++q) {
            Vec2 t = (c.sk.pts[(q + 1) % N] - c.sk.pts[(q + N - 1) % N]).norm();
            Vec2 p = c.sk.pts[q] + t.perp() * (gp.trackWidthU * mpu * 0.25);
            o << X(p.x) << "," << Y(p.y) << " ";
        }
        o << "' fill='none' stroke='" << kindColor(Kind::SkillRail) << "' stroke-width='2' stroke-dasharray='2,3'/>\n";
    }
    // direction arrows every ~0.8 m
    int every = std::max(4, (int)(0.8 / c.sk.stepM));
    for (int q = every / 2; q < N; q += every) {
        Vec2 p = c.sk.pts[q], t = (c.sk.pts[(q + 1) % N] - c.sk.pts[(q + N - 1) % N]).norm(), n = t.perp();
        double a = std::max(3.0, widthPx * 0.3);
        Vec2 tip = p + t * (a / s), l = p - t * (a / s) + n * (a / s * 0.7), rr = p - t * (a / s) - n * (a / s * 0.7);
        o << "<polygon points='" << X(tip.x) << "," << Y(tip.y) << " " << X(l.x) << "," << Y(l.y) << " " << X(rr.x) << "," << Y(rr.y)
          << "' fill='#ffffff' opacity='0.9'/>";
    }
    o << "\n";
    // rings
    for (auto& p : c.rings)
        o << "<circle cx='" << X(p.x) << "' cy='" << Y(p.y) << "' r='1.8' fill='#ffd60a' stroke='#b08900' stroke-width='0.6'/>";
    o << "\n";
    // start line
    {
        Vec2 p = c.sk.pts[0], t = (c.sk.pts[1] - c.sk.pts[N - 1]).norm(), n = t.perp();
        double hwM = gp.trackWidthU * mpu * 0.6;
        Vec2 a = p + n * hwM, b = p - n * hwM;
        o << "<line x1='" << X(a.x) << "' y1='" << Y(a.y) << "' x2='" << X(b.x) << "' y2='" << Y(b.y)
          << "' stroke='#000' stroke-width='5' stroke-dasharray='3,3'/>\n";
    }
    // legend
    {
        double lx = margin, ly = topH - 6;
        const Kind ks[] = {Kind::Track, Kind::Ramp, Kind::Dash, Kind::Spring, Kind::Drop, Kind::Gap, Kind::Rail, Kind::Loop, Kind::Homing, Kind::SkillRail};
        for (Kind k : ks) {
            o << "<rect x='" << lx << "' y='" << ly - 9 << "' width='14' height='9' fill='" << kindColor(k) << "'/>";
            o << "<text x='" << lx + 18 << "' y='" << ly << "' font-size='11' fill='#333'>" << kindName(k) << "</text>";
            lx += 82;
        }
        o << "\n";
    }
    // side profile
    {
        double px0 = margin, py0 = topH + 20, pw = W - 2 * margin, ph = profH;
        double L = c.sk.lengthM();
        double zmax = 2.0;
        auto PX = [&](double sM) { return px0 + sM / L * pw; };
        auto PY = [&](double z) { return py0 + ph - z / zmax * ph; };
        o << "<rect x='" << px0 << "' y='" << py0 << "' width='" << pw << "' height='" << ph << "' fill='#fafafa' stroke='#ccc'/>\n";
        o << "<text x='" << px0 << "' y='" << py0 - 6 << "' font-size='12' fill='#333'>Side profile: height (m) vs. distance along track (m); brown = furniture top under track</text>\n";
        for (double z : {gp.prefMinH, gp.maxH}) {
            o << "<line x1='" << px0 << "' y1='" << PY(z) << "' x2='" << px0 + pw << "' y2='" << PY(z)
              << "' stroke='#bbb' stroke-dasharray='4,4'/>";
            std::snprintf(buf, sizeof buf, "%.1f m", z);
            o << "<text x='" << px0 + pw - 30 << "' y='" << PY(z) - 3 << "' font-size='10' fill='#999'>" << buf << "</text>\n";
        }
        o << "<line x1='" << px0 << "' y1='" << PY(0) << "' x2='" << px0 + pw << "' y2='" << PY(0) << "' stroke='#555' stroke-width='2'/>\n";
        for (int q = 0; q < N; ++q)
            if (c.prof.box[q] >= 0) {
                double h = room.boxes[c.prof.box[q]].height;
                o << "<rect x='" << PX(q * c.sk.stepM) << "' y='" << PY(h) << "' width='" << std::max(1.0, pw / N + 0.5)
                  << "' height='" << PY(0) - PY(h) << "' fill='#d8c3a0' opacity='0.8'/>";
            }
        o << "\n";
        for (auto& m : c.mods) {
            const char* col = kindColor(m.kind);
            std::ostringstream pts;
            pts.setf(std::ios::fixed); pts.precision(1);
            for (int q = m.s0; q <= m.s1 && q <= N; ++q) {
                double z = q < N && q < m.s1 ? c.z[q] : m.h1;
                pts << PX(q * c.sk.stepM) << "," << PY(z) << " ";
            }
            bool air = m.kind == Kind::Spring || m.kind == Kind::Gap || m.kind == Kind::Homing;
            o << "<polyline points='" << pts.str() << "' fill='none' stroke='" << col << "' stroke-width='" << (air ? 2 : 4)
              << "'" << (air ? " stroke-dasharray='5,3'" : "") << "/>\n";
            if (m.kind == Kind::Loop) {
                double rM = lib.defs[m.def].param("radius", 22) * mpu;
                double mid = (m.s0 + m.s1) * 0.5 * c.sk.stepM;
                o << "<ellipse cx='" << PX(mid) << "' cy='" << PY(m.h0 + rM) << "' rx='" << rM / L * pw << "' ry='" << rM / zmax * ph
                  << "' fill='none' stroke='" << col << "' stroke-width='3'/>\n";
            }
        }
        for (auto& sr : c.skill) {
            o << "<line x1='" << PX(sr.s0 * c.sk.stepM) << "' y1='" << PY(sr.zM) << "' x2='" << PX(sr.s1 * c.sk.stepM)
              << "' y2='" << PY(sr.zM) << "' stroke='" << kindColor(Kind::SkillRail) << "' stroke-width='2' stroke-dasharray='2,3'/>\n";
        }
        for (double m = 0; m <= L; m += 1.0) {
            std::snprintf(buf, sizeof buf, "%.0f", m);
            o << "<text x='" << PX(m) << "' y='" << py0 + ph + 14 << "' font-size='10' text-anchor='middle' fill='#666'>" << buf << "</text>";
        }
        o << "\n";
    }
    o << "</svg>\n";
    return o.str();
}

// ---------------------------------------------------------------------------- OBJ

namespace {
struct ObjWriter {
    std::ostringstream o;
    int nv = 0;
    ObjWriter() { o.setf(std::ios::fixed); o.precision(4); }
    // room coords (x, y, z-up) -> OBJ Y-up
    int v(double x, double y, double z) { o << "v " << x << " " << z << " " << -y << "\n"; return ++nv; }
    void f(int a, int b, int c) { o << "f " << a << " " << b << " " << c << "\n"; }
    void quad(int a, int b, int c, int d) { o << "f " << a << " " << b << " " << c << " " << d << "\n"; }
    void box(Vec3 c, double hx, double hy, double hz, double yaw = 0) {
        double cs = std::cos(yaw), sn = std::sin(yaw);
        int base = nv + 1;
        for (int k = 0; k < 8; ++k) {
            double lx = (k & 1 ? hx : -hx), ly = (k & 2 ? hy : -hy), lz = (k & 4 ? hz : -hz);
            v(c.x + lx * cs - ly * sn, c.y + lx * sn + ly * cs, c.z + lz);
        }
        auto q = [&](int a, int b, int cc, int d) { quad(base + a, base + b, base + cc, base + d); };
        q(0, 1, 3, 2); q(4, 6, 7, 5); q(0, 4, 5, 1); q(2, 3, 7, 6); q(0, 2, 6, 4); q(1, 5, 7, 3);
    }
};

std::vector<int> earClip(const Polygon& p) {
    std::vector<int> idx(p.size()), tris;
    for (size_t i = 0; i < p.size(); ++i) idx[i] = (int)i;
    int guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); ++i) {
            int a = idx[(i + idx.size() - 1) % idx.size()], b = idx[i], c = idx[(i + 1) % idx.size()];
            if ((p[b] - p[a]).cross(p[c] - p[b]) <= 0) continue;  // reflex (CCW polygon)
            bool inside = false;
            for (int k : idx) {
                if (k == a || k == b || k == c) continue;
                Vec2 q = p[k];
                double d1 = (p[b] - p[a]).cross(q - p[a]), d2 = (p[c] - p[b]).cross(q - p[b]), d3 = (p[a] - p[c]).cross(q - p[c]);
                if (d1 >= 0 && d2 >= 0 && d3 >= 0) { inside = true; break; }
            }
            if (inside) continue;
            tris.insert(tris.end(), {a, b, c});
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) tris.insert(tris.end(), {idx[0], idx[1], idx[2]});
    return tris;
}
}  // namespace

std::string courseToObj(const GenResult& r, const ModuleLibrary& lib, const GenParams& gp) {
    const Course& c = r.course;
    const Room& room = r.room;
    const double mpu = gp.metersPerUnit();
    const double hw = 0.5 * gp.trackWidthU * mpu, thick = 2 * mpu, wallH = 4 * mpu;
    const int N = c.sk.size();
    ObjWriter w;
    w.o << "# Room Run course (" << kGeneratorVersion << "), meters, Y-up\n";
    auto frame = [&](int j, Vec2& p, Vec2& n) {
        p = c.sk.pts[j % N];
        n = (c.sk.pts[(j + 1) % N] - c.sk.pts[(j + N - 1) % N]).norm().perp();
    };
    // track surface + walls
    w.o << "o track\n";
    for (int j = 0; j < N; ++j) {
        int jn = (j + 1) % N;
        if (!c.exists[j] || !c.exists[jn] || c.rail[j] || std::fabs(c.z[jn] - c.z[j]) > 3.5 * mpu) continue;
        Vec2 p0, n0, p1, n1;
        frame(j, p0, n0); frame(jn, p1, n1);
        double z0 = c.z[j], z1 = c.z[jn];
        int a = w.v(p0.x + n0.x * hw, p0.y + n0.y * hw, z0), b = w.v(p0.x - n0.x * hw, p0.y - n0.y * hw, z0);
        int cc = w.v(p1.x - n1.x * hw, p1.y - n1.y * hw, z1), d = w.v(p1.x + n1.x * hw, p1.y + n1.y * hw, z1);
        int a2 = w.v(p0.x + n0.x * hw, p0.y + n0.y * hw, z0 - thick), b2 = w.v(p0.x - n0.x * hw, p0.y - n0.y * hw, z0 - thick);
        int c2 = w.v(p1.x - n1.x * hw, p1.y - n1.y * hw, z1 - thick), d2 = w.v(p1.x + n1.x * hw, p1.y + n1.y * hw, z1 - thick);
        w.quad(a, b, cc, d);
        w.quad(d2, c2, b2, a2);
        if (c.walled[j]) {
            int wa = w.v(p0.x + n0.x * hw, p0.y + n0.y * hw, z0 + wallH), wd = w.v(p1.x + n1.x * hw, p1.y + n1.y * hw, z1 + wallH);
            int wb = w.v(p0.x - n0.x * hw, p0.y - n0.y * hw, z0 + wallH), wc = w.v(p1.x - n1.x * hw, p1.y - n1.y * hw, z1 + wallH);
            w.quad(a2, a, d, d2); w.quad(a, wa, wd, d);
            w.quad(b, b2, c2, cc); w.quad(wb, b, cc, wc);
        }
    }
    // rails (square tube)
    w.o << "o rails\n";
    const double rr = 1.0 * mpu;
    for (int j = 0; j < N; ++j) {
        int jn = (j + 1) % N;
        if (!c.rail[j] || !c.exists[jn]) continue;
        Vec2 p0, n0, p1, n1;
        frame(j, p0, n0); frame(jn, p1, n1);
        double z0 = c.z[j] + 2 * mpu, z1 = c.z[jn] + 2 * mpu;
        int v0[4], v1[4];
        const double ox[4] = {rr, -rr, -rr, rr}, oz[4] = {rr, rr, -rr, -rr};
        for (int k = 0; k < 4; ++k) {
            v0[k] = w.v(p0.x + n0.x * ox[k], p0.y + n0.y * ox[k], z0 + oz[k]);
            v1[k] = w.v(p1.x + n1.x * ox[k], p1.y + n1.y * ox[k], z1 + oz[k]);
        }
        for (int k = 0; k < 4; ++k) w.quad(v0[k], v0[(k + 1) % 4], v1[(k + 1) % 4], v1[k]);
    }
    // supports: pillars every ~0.4 m down to the floor or furniture top
    w.o << "o supports\n";
    int every = std::max(2, (int)(0.4 / c.sk.stepM));
    for (int j = 0; j < N; j += every) {
        if (!c.exists[j]) continue;
        double base = 0;
        for (auto& b : room.boxes) if (b.rect.contains(c.sk.pts[j])) base = std::max(base, b.height);
        double top = c.z[j] - thick;
        if (top - base < 0.02) continue;
        // skip if a lower deck is directly below (stacked layers rest on their own pillars)
        double half = 1.0 * mpu;
        w.box({c.sk.pts[j].x, c.sk.pts[j].y, (top + base) / 2}, half, half, (top - base) / 2);
    }
    // loops
    w.o << "o loops\n";
    for (auto& m : c.mods) {
        if (m.kind != Kind::Loop) continue;
        double R = lib.defs[m.def].param("radius", 22) * mpu;
        int mid = (m.s0 + m.s1) / 2;
        Vec2 p, n;
        frame(mid, p, n);
        Vec2 t = n.perp() * -1.0;  // tangent
        const int seg = 32;
        int prevA = 0, prevB = 0;
        for (int k = 0; k <= seg; ++k) {
            double th = 2 * kPi * k / seg;
            double lat = (k / (double)seg - 0.5) * hw * 2;  // corkscrew offset so entry/exit don't collide
            Vec2 xy = p + t * (R * std::sin(th)) + n * lat;
            double z = m.h0 + R * (1 - std::cos(th));
            int a = w.v(xy.x + n.x * hw, xy.y + n.y * hw, z), b = w.v(xy.x - n.x * hw, xy.y - n.y * hw, z);
            if (k) w.quad(prevA, prevB, b, a);
            prevA = a; prevB = b;
        }
    }
    // spring pads, dash panels, homing targets
    w.o << "o props\n";
    for (auto& m : c.mods) {
        Vec2 p, n;
        if (m.kind == Kind::Spring) { frame(m.s0 + m.padN / 2, p, n); w.box({p.x, p.y, m.h0 + 1.5 * mpu}, 5 * mpu, 5 * mpu, 1.5 * mpu); }
        if (m.kind == Kind::Dash) { frame((m.s0 + m.s1) / 2, p, n); w.box({p.x, p.y, m.h0 + 0.3 * mpu}, 6 * mpu, 4 * mpu, 0.3 * mpu); }
        if (m.kind == Kind::Homing) {
            const ModuleDef& d = lib.defs[m.def];
            double a = m.s0 + m.lipN, b = m.s1 - m.padN;
            int K = std::max(1, (int)((b - a) * gp.stationU / d.param("spacing", 32)));
            for (int k = 0; k < K; ++k) {
                double f = (k + 0.5) / K;
                int j = (int)(a + (b - a) * f);
                frame(j, p, n);
                w.box({p.x, p.y, m.h0 + (m.h1 - m.h0) * f + d.param("lift", 12) * mpu}, 3 * mpu, 3 * mpu, 3 * mpu);
            }
        }
    }
    w.o << "o skill_rails\n";
    for (auto& sr : c.skill) {
        for (int j = sr.s0; j + 1 < sr.s1; ++j) {
            Vec2 p0, n0, p1, n1;
            frame(j, p0, n0); frame(j + 1, p1, n1);
            int v0[4], v1[4];
            const double ox[4] = {rr, -rr, -rr, rr}, oz[4] = {rr, rr, -rr, -rr};
            for (int k = 0; k < 4; ++k) {
                v0[k] = w.v(p0.x + n0.x * ox[k], p0.y + n0.y * ox[k], sr.zM + oz[k]);
                v1[k] = w.v(p1.x + n1.x * ox[k], p1.y + n1.y * ox[k], sr.zM + oz[k]);
            }
            for (int k = 0; k < 4; ++k) w.quad(v0[k], v0[(k + 1) % 4], v1[(k + 1) % 4], v1[k]);
        }
    }
    w.o << "o rings\n";
    for (auto& rg : c.rings) w.box(rg, 1.5 * mpu, 1.5 * mpu, 1.5 * mpu);
    // room context
    w.o << "o floor\n";
    {
        int base = w.nv + 1;
        for (auto& p : room.floor) w.v(p.x, p.y, 0);
        auto tris = earClip(room.floor);
        for (size_t k = 0; k + 2 < tris.size(); k += 3) w.f(base + tris[k], base + tris[k + 1], base + tris[k + 2]);
    }
    w.o << "o furniture\n";
    for (auto& b : room.boxes) w.box({b.rect.c.x, b.rect.c.y, b.height / 2}, b.rect.half.x, b.rect.half.y, b.height / 2, b.rect.yaw);
    w.o << "o player_cylinder\n";
    {
        const int seg = 24;
        int base = w.nv + 1;
        for (int k = 0; k < seg; ++k) {
            double a = 2 * kPi * k / seg;
            w.v(room.player.x + room.playerRadius * std::cos(a), room.player.y + room.playerRadius * std::sin(a), 0);
            w.v(room.player.x + room.playerRadius * std::cos(a), room.player.y + room.playerRadius * std::sin(a), 1.8);
        }
        for (int k = 0; k < seg; ++k) {
            int a = base + 2 * k, b = base + 2 * ((k + 1) % seg);
            w.quad(a, b, b + 1, a + 1);
        }
    }
    return w.o.str();
}

}  // namespace rr
