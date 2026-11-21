#include "roomrun/field.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace rr {

double Grid::sample(Vec2 p) const {
    double fx = (p.x - x0) / res, fy = (p.y - y0) / res;
    int i = (int)std::floor(fx), j = (int)std::floor(fy);
    if (i < 0 || j < 0 || i + 1 >= nx || j + 1 >= ny) return -1.0;
    double tx = fx - i, ty = fy - j;
    double a = at(i, j) * (1 - tx) + at(i + 1, j) * tx;
    double b = at(i, j + 1) * (1 - tx) + at(i + 1, j + 1) * tx;
    return a * (1 - ty) + b * ty;
}

FreeSpace buildFreeSpace(const Room& room, const GenParams& gp) {
    FreeSpace fs;
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    for (auto& p : room.floor) {
        minx = std::min(minx, p.x); miny = std::min(miny, p.y);
        maxx = std::max(maxx, p.x); maxy = std::max(maxy, p.y);
    }
    double res = gp.gridResM;
    double pad = 2 * res;
    int nx = (int)std::ceil((maxx - minx + 2 * pad) / res) + 1;
    int ny = (int)std::ceil((maxy - miny + 2 * pad) / res) + 1;
    Grid& F = fs.clearance;
    F.init(minx - pad, miny - pad, res, nx, ny, -1.0f);
    for (int j = 1; j < ny - 1; ++j)
        for (int i = 1; i < nx - 1; ++i) {
            Vec2 p = F.pos(i, j);
            double d = signedDistPolygon(room.floor, p) - gp.wallMarginM;
            d = std::min(d, (p - room.player).len() - room.playerRadius);
            for (auto& b : room.boxes)
                if (b.height > gp.tallBoxM) d = std::min(d, b.rect.dist(p) - gp.boxMarginM);
            F.at(i, j) = (float)d;
        }
    // Medial axis: free cells that are a local maximum of clearance along some axis.
    fs.ridge.assign((size_t)nx * ny, 0);
    const int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    for (int j = 1; j < ny - 1; ++j)
        for (int i = 1; i < nx - 1; ++i) {
            float c = F.at(i, j);
            if (c <= 0.05f) continue;
            for (auto& d : dirs) {
                float a = F.at(i - d[0], j - d[1]), b = F.at(i + d[0], j + d[1]);
                if (c >= a && c >= b && c > std::min(a, b) + 0.3f * (float)res) { fs.ridge[(size_t)j * nx + i] = 1; break; }
            }
        }
    // Medial vertices = 8-neighbourhood maxima with non-max suppression.
    std::vector<MedialPoint> cand;
    for (int j = 1; j < ny - 1; ++j)
        for (int i = 1; i < nx - 1; ++i) {
            float c = F.at(i, j);
            if (c < 0.12f) continue;
            bool mx = true;
            for (int dj = -1; dj <= 1 && mx; ++dj)
                for (int di = -1; di <= 1; ++di)
                    if ((di || dj) && F.at(i + di, j + dj) > c) { mx = false; break; }
            if (mx) cand.push_back({F.pos(i, j), c});
        }
    std::stable_sort(cand.begin(), cand.end(), [](const MedialPoint& a, const MedialPoint& b) { return a.r > b.r; });
    for (auto& c : cand) {
        bool keep = true;
        for (auto& m : fs.maxima)
            if ((m.p - c.p).len() < std::max(0.3, 0.9 * m.r)) { keep = false; break; }
        if (keep) fs.maxima.push_back(c);
        if (fs.maxima.size() >= 16) break;
    }
    return fs;
}

std::vector<float> edt(const std::vector<uint8_t>& mask, int nx, int ny, double res) {
    const double INF = 1e12;
    std::vector<double> f((size_t)nx * ny);
    for (size_t k = 0; k < f.size(); ++k) f[k] = mask[k] ? 0.0 : INF;
    auto pass = [&](int n, auto get, auto put) {
        std::vector<double> g(n), z(n + 1);
        std::vector<int> v(n);
        for (int q = 0; q < n; ++q) g[q] = get(q);
        int k = 0;
        v[0] = 0; z[0] = -1e30; z[1] = 1e30;
        auto inter = [&](int q, int p) { return ((g[q] + (double)q * q) - (g[p] + (double)p * p)) / (2.0 * q - 2.0 * p); };
        for (int q = 1; q < n; ++q) {
            double s = inter(q, v[k]);
            while (s <= z[k]) { --k; s = inter(q, v[k]); }
            ++k; v[k] = q; z[k] = s; z[k + 1] = 1e30;
        }
        k = 0;
        for (int q = 0; q < n; ++q) {
            while (z[k + 1] < q) ++k;
            double dq = q - v[k];
            put(q, dq * dq + g[v[k]]);
        }
    };
    for (int i = 0; i < nx; ++i)
        pass(ny, [&](int q) { return f[(size_t)q * nx + i]; }, [&](int q, double val) { f[(size_t)q * nx + i] = val; });
    for (int j = 0; j < ny; ++j)
        pass(nx, [&](int q) { return f[(size_t)j * nx + q]; }, [&](int q, double val) { f[(size_t)j * nx + q] = val; });
    std::vector<float> out(f.size());
    for (size_t k = 0; k < f.size(); ++k) out[k] = f[k] >= INF * 0.5 ? 1e9f : (float)(std::sqrt(f[k]) * res);
    return out;
}

Grid openedField(const Grid& F, double c, double r) {
    std::vector<uint8_t> core(F.v.size());
    for (size_t k = 0; k < F.v.size(); ++k) core[k] = F.v[k] >= c + r ? 1 : 0;
    std::vector<float> D = edt(core, F.nx, F.ny, F.res);
    Grid G;
    G.init(F.x0, F.y0, F.res, F.nx, F.ny);
    for (size_t k = 0; k < G.v.size(); ++k) G.v[k] = (float)(r - D[k]);
    // keep border negative so contours close
    for (int i = 0; i < G.nx; ++i) { G.at(i, 0) = -1; G.at(i, G.ny - 1) = -1; }
    for (int j = 0; j < G.ny; ++j) { G.at(0, j) = -1; G.at(G.nx - 1, j) = -1; }
    return G;
}

std::vector<std::vector<Vec2>> contourLoops(const Grid& g, double level) {
    int nx = g.nx, ny = g.ny;
    auto H = [&](int i, int j) { return ((size_t)j * nx + i) * 2; };      // edge (i,j)-(i+1,j)
    auto V = [&](int i, int j) { return ((size_t)j * nx + i) * 2 + 1; };  // edge (i,j)-(i,j+1)
    size_t E = (size_t)nx * ny * 2;
    std::vector<std::array<int64_t, 2>> adj(E, {-1, -1});
    std::vector<Vec2> pt(E);
    auto edgePoint = [&](size_t id) {
        size_t cell = id / 2;
        int i = (int)(cell % nx), j = (int)(cell / nx);
        int i2 = i + ((id & 1) ? 0 : 1), j2 = j + ((id & 1) ? 1 : 0);
        double a = g.at(i, j), b = g.at(i2, j2);
        double t = std::fabs(b - a) > 1e-12 ? (level - a) / (b - a) : 0.5;
        t = std::clamp(t, 0.0, 1.0);
        Vec2 pa = g.pos(i, j), pb = g.pos(i2, j2);
        return pa + (pb - pa) * t;
    };
    auto link = [&](size_t a, size_t b) {
        for (size_t e : {a, b}) pt[e] = edgePoint(e);
        if (adj[a][0] < 0) adj[a][0] = (int64_t)b; else adj[a][1] = (int64_t)b;
        if (adj[b][0] < 0) adj[b][0] = (int64_t)a; else adj[b][1] = (int64_t)a;
    };
    for (int j = 0; j + 1 < ny; ++j)
        for (int i = 0; i + 1 < nx; ++i) {
            double v00 = g.at(i, j), v10 = g.at(i + 1, j), v11 = g.at(i + 1, j + 1), v01 = g.at(i, j + 1);
            int code = (v00 > level) | ((v10 > level) << 1) | ((v11 > level) << 2) | ((v01 > level) << 3);
            if (code == 0 || code == 15) continue;
            size_t e[4] = {H(i, j), V(i + 1, j), H(i, j + 1), V(i, j)};  // bottom,right,top,left
            bool center = (v00 + v10 + v11 + v01) * 0.25 > level;
            switch (code) {
                case 1: case 14: link(e[0], e[3]); break;
                case 2: case 13: link(e[0], e[1]); break;
                case 3: case 12: link(e[3], e[1]); break;
                case 4: case 11: link(e[1], e[2]); break;
                case 6: case 9: link(e[0], e[2]); break;
                case 7: case 8: link(e[3], e[2]); break;
                case 5:
                    if (center) { link(e[0], e[1]); link(e[2], e[3]); }
                    else { link(e[0], e[3]); link(e[1], e[2]); }
                    break;
                case 10:
                    if (center) { link(e[0], e[3]); link(e[1], e[2]); }
                    else { link(e[0], e[1]); link(e[2], e[3]); }
                    break;
            }
        }
    std::vector<uint8_t> seen(E, 0);
    std::vector<std::vector<Vec2>> loops;
    for (size_t s = 0; s < E; ++s) {
        if (adj[s][0] < 0 || seen[s]) continue;
        std::vector<Vec2> loop;
        int64_t prev = -1, cur = (int64_t)s;
        bool closed = false;
        while (cur >= 0 && !seen[cur]) {
            seen[cur] = 1;
            loop.push_back(pt[cur]);
            int64_t nxt = adj[cur][0] != prev ? adj[cur][0] : adj[cur][1];
            if (nxt == (int64_t)s) closed = true;
            prev = cur;
            cur = nxt;
        }
        if (closed && loop.size() >= 8) loops.push_back(std::move(loop));
    }
    return loops;
}

}  // namespace rr
