#include "roomrun/assembler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rr {

namespace {

struct Node {
    int st = 0;
    double h = 0;
    Envelope env;
    double score = 0;
    int parent = -1;
    Placed pm;
    uint32_t used = 0;
    uint8_t count[16] = {};
    int lastKind = -1;
    int lastCls = -1;
    int lastDef = -1;
    int railSt = 0;   // rail stations so far
};

// Sparse table for range-max of |curvature| (cyclic range never needed: modules stay in [0,N)).
struct RangeMax {
    std::vector<std::vector<double>> t;
    void build(const std::vector<double>& v) {
        int n = (int)v.size();
        t.assign(1, std::vector<double>(n));
        for (int i = 0; i < n; ++i) t[0][i] = std::fabs(v[i]);
        for (int k = 1; (1 << k) <= n; ++k) {
            t.emplace_back(n);
            for (int i = 0; i + (1 << k) <= n; ++i) t[k][i] = std::max(t[k - 1][i], t[k - 1][i + (1 << (k - 1))]);
        }
    }
    double query(int a, int b) const {  // [a, b)
        int len = b - a, k = 31 - __builtin_clz((unsigned)len);
        return std::max(t[k][a], t[k][b - (1 << k)]);
    }
};

bool isAirOrVertical(Kind k) {
    return k == Kind::Spring || k == Kind::Drop || k == Kind::Gap || k == Kind::Homing || k == Kind::Loop;
}

class Assembler {
public:
    Assembler(const Skeleton& sk, const Profile& pr, const ModuleLibrary& lib, const GenParams& gp, Rng& rng,
              const AssembleOptions& opt)
        : sk_(sk), pr_(pr), lib_(lib), gp_(gp), rng_(rng), opt_(opt) {
        N_ = sk.size();
        mpu_ = gp.metersPerUnit();
        stepU_ = gp.stationU;
        gapM_ = gp.layerGapU * mpu_;
        rmq_.build(sk.curv);
        partnerPrefix_.assign(N_ + 1, 0);
        for (int i = 0; i < N_; ++i) partnerPrefix_[i + 1] = partnerPrefix_[i] + (sk.partners[i].empty() ? 0 : 1);
    }

    bool run(std::vector<Placed>& out, double* score) {
        int startDef = lib_.findKind(Kind::Start);
        if (startDef < 0) return false;
        const ModuleDef& sd = lib_.defs[startDef];
        int n0 = (int)std::ceil(sd.lenMin / stepU_);
        double maxL = 0;
        for (int j = 0; j < n0; ++j) maxL = std::max(maxL, pr_.L[j]);
        hStart_ = std::clamp(pr_.T[0], std::max(maxL, gp_.prefMinH), gp_.maxH);
        hStart_ = std::round(hStart_ / 0.01) * 0.01;
        if (hStart_ < maxL) hStart_ += 0.01;
        ScaledPhysics sp(gp_.physics, gp_.speedScale);
        Node root;
        root.env = {0, sp.p.topSpeed, 99, 180};
        root.h = hStart_;
        Placed m;
        m.def = startDef; m.kind = Kind::Start; m.s0 = 0; m.s1 = n0; m.h0 = m.h1 = hStart_;
        m.in = root.env;
        m.out = transferEnvelope(m, sd, root.env, sk_, gp_);
        root.pm = m;
        root.env = m.out;
        root.st = n0;
        root.lastKind = (int)Kind::Start;
        root.lastCls = (int)MClass::Forgiving;
        buckets_.assign(N_ + 1, {});
        buckets_[n0].push_back(root);
        for (int st = 0; st < N_; ++st) {
            auto& b = buckets_[st];
            if (b.empty()) continue;
            std::stable_sort(b.begin(), b.end(), [](const Node& x, const Node& y) { return x.score > y.score; });
            std::vector<int> survivors;
            for (auto& nd : b) {
                bool similar = false;
                for (int s : survivors) {
                    const Node& o = pool_[s];
                    if (o.lastKind == nd.lastKind && std::fabs(o.h - nd.h) < 0.03 && std::fabs(o.env.vMin - nd.env.vMin) < 0.3)
                        similar = true;
                }
                if (similar) continue;
                pool_.push_back(nd);
                survivors.push_back((int)pool_.size() - 1);
                if ((int)survivors.size() >= gp_.beamWidth) break;
            }
            b.clear();
            b.shrink_to_fit();
            for (int s : survivors) expand(s);
        }
        auto& fin = buckets_[N_];
        if (fin.empty()) {
            if (gp_.debug) {
                int far = 0;
                int farIdx = 0;
                for (size_t q = 0; q < pool_.size(); ++q) if (pool_[q].st > far) { far = pool_[q].st; farIdx = (int)q; }
                const Node& fn = pool_[farIdx];
                std::fprintf(stderr, "  far node h=%.2f T=%.2f L=%.2f env=[%.2f,%.2f] last=%s\n", fn.h, pr_.T[fn.st % N_], pr_.L[fn.st % N_], fn.env.vMin, fn.env.vMax, kindName((Kind)fn.lastKind));
                for (int j = fn.st; j < std::min(N_, fn.st + 40); j += 4) {
                    std::fprintf(stderr, "    st %d T=%.2f L=%.2f k=%.3f partners:", j, pr_.T[j], pr_.L[j], sk_.curv[j]);
                    for (int p : sk_.partners[j]) if (p < fn.st) std::fprintf(stderr, " %d@%.2f", p, heightAt(farIdx, p));
                    std::fprintf(stderr, "\n");
                }
                std::fprintf(stderr, "  assembler: furthest station %d / %d (pool %zu)\n", far, N_, pool_.size());
            }
            return false;
        }
        const Node* best = &fin[0];
        for (auto& nd : fin) if (nd.score > best->score) best = &nd;
        std::vector<Placed> rev;
        rev.push_back(best->pm);
        for (int p = best->parent; p >= 0; p = pool_[p].parent) rev.push_back(pool_[p].pm);
        out.assign(rev.rbegin(), rev.rend());
        if (score) *score = best->score;
        return true;
    }

private:
    double heightAt(int nodeIdx, int j) const {
        for (int p = nodeIdx; p >= 0; p = pool_[p].parent) {
            const Placed& m = pool_[p].pm;
            if (j >= m.s0 && j < m.s1) return stationInfo(m, lib_.defs[m.def], j, gp_).z;
        }
        return 0;
    }

    void push(Node&& c) {
        auto& b = buckets_[c.st];
        size_t cap = (size_t)gp_.beamWidth * 4;
        if (b.size() < cap) { b.push_back(std::move(c)); return; }
        size_t worst = 0;
        for (size_t i = 1; i < b.size(); ++i) if (b[i].score < b[worst].score) worst = i;
        if (c.score > b[worst].score) b[worst] = std::move(c);
    }

    bool banned(Kind k, int s0, int s1) const {
        for (auto& b : opt_.bans)
            if (b.kind == k && s0 < b.s1 && b.s0 < s1) return true;
        return false;
    }

    void tryPlace(int parentIdx, int defIdx, int n, double dhU, int lipN, int holeN, int padN) {
        const Node& par = pool_[parentIdx];
        const ModuleDef& d = lib_.defs[defIdx];
        int s0 = par.st, s1 = s0 + n;
        if (n <= 0 || s1 > N_) return;
        int rem = N_ - s1;
        if (rem != 0 && rem < 4) return;
        Placed m;
        m.def = defIdx; m.kind = d.kind; m.s0 = s0; m.s1 = s1;
        m.h0 = par.h; m.h1 = par.h + dhU * mpu_;
        m.lipN = lipN; m.holeN = holeN; m.padN = padN;
        if (s1 == N_) {
            if (std::fabs(m.h1 - hStart_) > 1e-6) return;
            m.h1 = hStart_;
        }
        if (m.h1 < gp_.minH - 1e-9 || m.h1 > gp_.maxH + 1e-9) return;
        if (rmq_.query(s0, s1) > d.maxCurv) return;
        if (isAirOrVertical(d.kind) && partnerPrefix_[s1] - partnerPrefix_[s0] > 0) return;
        if (banned(d.kind, s0, s1)) return;
        if (!par.env.subsetOf(d.entry)) return;
        double dev = 0, visit = 0;
        for (int j = s0; j < s1; ++j) {
            StationInfo si = stationInfo(m, d, j, gp_);
            if (si.z < pr_.L[j] - 1e-9) return;
            if (si.z > (si.exists ? pr_.U[j] : gp_.maxH + 0.6) + 1e-9) return;
            for (int p : sk_.partners[j]) {
                double hp;
                if (p < s0) hp = chainZ_[p];
                else if (p < j) hp = stationInfo(m, d, p, gp_).z;
                else continue;
                if (std::fabs(si.z - hp) < gapM_) return;
            }
            dev += std::fabs(si.z - pr_.T[j]);
            if (pr_.box[j] >= 0 && si.exists && si.z - pr_.L[j] < 0.015) visit += 1;
        }
        Node c;
        c.st = s1;
        c.h = m.h1;
        c.parent = parentIdx;
        m.in = par.env;
        m.out = transferEnvelope(m, d, par.env, sk_, gp_);
        c.env = m.out;
        c.pm = m;
        uint32_t bit = 1u << (int)d.kind;
        // diminishing returns per kind keeps the mix varied
        double sc = d.fun * (0.85 + 0.3 * rng_.uniform()) / (1.0 + 0.5 * par.count[(int)d.kind]);
        double lenM = n * sk_.stepM;
        if (d.cls == MClass::Forgiving && d.walls) sc += 0.12 * lenM;      // solid walled track is the backbone
        if (d.cls == MClass::Forgiving && n * stepU_ < 40) sc -= 0.3;       // no tiny filler pieces
        sc -= 3.0 * dev * sk_.stepM;
        sc += 0.03 * visit;
        if (!(par.used & bit) && d.cls != MClass::Forgiving) sc += 0.6;
        if (defIdx == par.lastDef && d.fun > 0.1) sc -= 0.8;  // no identical set pieces back to back
        bool setPiece = d.cls != MClass::Forgiving || d.kind == Kind::Drop;
        bool prevSet = par.lastCls != (int)MClass::Forgiving;
        bool combo = par.lastKind == (int)Kind::Dash && (d.kind == Kind::Gap || d.kind == Kind::Loop || d.kind == Kind::Drop);
        if (setPiece && prevSet && !combo) sc -= 0.5;
        if (rem < 30) sc -= 0.5 * std::fabs(m.h1 - hStart_) / 0.1;
        // variety over long rail runs (the Frame, 2026-11-21: a 2 x 2 m course came out 78% rail): rail beyond
        // gp_.railShareMax of the course costs, so road (where springs, panels and enemies go) wins where it fits
        const int railAfter = par.railSt + (d.kind == Kind::Rail ? n : 0);
        if (d.kind == Kind::Rail && railAfter > gp_.railShareMax * N_)
            sc -= gp_.railExcessCost * (railAfter - std::max((double)par.railSt, gp_.railShareMax * N_)) / 10.0;
        c.score = par.score + sc;
        c.used = par.used | bit;
        std::copy(par.count, par.count + 16, c.count);
        if (c.count[(int)d.kind] < 255) c.count[(int)d.kind]++;
        c.lastKind = (int)d.kind;
        c.lastDef = defIdx;
        c.railSt = par.railSt + (d.kind == Kind::Rail ? n : 0);
        c.lastCls = d.kind == Kind::Drop ? (int)MClass::Mover : (int)d.cls;
        push(std::move(c));
    }

    void expand(int idx) {
        const Node par = pool_[idx];
        if (partnerPrefix_[N_] > 0) {
            chainZ_.assign(N_, 0.0);
            for (int p = idx; p >= 0; p = pool_[p].parent) {
                const Placed& m = pool_[p].pm;
                for (int j = m.s0; j < m.s1; ++j) chainZ_[j] = stationInfo(m, lib_.defs[m.def], j, gp_).z;
            }
        }
        int i = par.st, rem = N_ - i;
        for (int di = 0; di < (int)lib_.defs.size(); ++di) {
            const ModuleDef& d = lib_.defs[di];
            if (d.kind == Kind::Start || d.cls == MClass::Skill) continue;
            if (!par.env.subsetOf(d.entry)) continue;
            int nMin = (int)std::ceil(d.lenMin / stepU_ - 1e-9), nMax = (int)std::floor(d.lenMax / stepU_ + 1e-9);
            int nStep = std::max(1, (int)std::round(d.lenStep / stepU_));
            auto tgtFor = [&](int n) {
                int e = i + n;
                if (e >= N_) return (hStart_ - par.h) / mpu_;
                return (pr_.T[e] - par.h) / mpu_;
            };
            if (d.kind == Kind::Gap || d.kind == Kind::Drop) {
                // length follows from physics; target the height ~1 m ahead
                double tgt = tgtFor(std::min(rem, (int)(100 / stepU_)));
                double dh = std::clamp(tgt, d.dhMin, d.dhMax);
                if (d.kind == Kind::Drop && tgt > d.dhMax) continue;
                for (int attempt = 0; attempt < 2; ++attempt, dh *= 0.6) {
                    if (d.kind == Kind::Drop && dh > d.dhMax) break;
                    int n = 0, lip = 0, hole = 0;
                    bool ok = d.kind == Kind::Gap ? sizeGap(d, par.env, dh, gp_, n, lip, hole)
                                                  : sizeDrop(d, par.env, dh, gp_, n, lip);
                    if (!ok) continue;
                    if (i + n == N_) dh = (hStart_ - par.h) / mpu_;
                    tryPlace(idx, di, n, dh, lip, hole, 0);
                    break;
                }
                continue;
            }
            std::vector<int> ns;
            auto add = [&](int n) {
                if (n >= nMin && n <= nMax && n <= rem && std::find(ns.begin(), ns.end(), n) == ns.end()) ns.push_back(n);
            };
            add(nMin);
            int steps = (nMax - nMin) / nStep;
            if (steps > 0) {
                add(nMin + nStep * rng_.irange(1, steps));
                add(nMin + nStep * rng_.irange(1, steps));
                add(nMin + nStep * steps);
            }
            add(rem);
            if (d.kind == Kind::Track) { add(rem - nMin); add(rem - 2 * nMin); }
            for (int n : ns) {
                bool closing = i + n == N_;
                double tgt = tgtFor(n);
                double lenU = n * stepU_;
                std::vector<double> dhs;
                switch (d.kind) {
                    case Kind::Track: case Kind::Dash: case Kind::Loop: dhs = {0.0}; break;
                    case Kind::Ramp: case Kind::Rail: {
                        double mx = d.maxSlope * lenU * 2 / kPi;
                        double dh = std::clamp(tgt, std::max(d.dhMin, -mx), std::min(d.dhMax, mx));
                        if (d.kind == Kind::Ramp && std::fabs(dh) < 6 && !closing) break;
                        dhs = {dh};
                        break;
                    }
                    case Kind::Spring: {
                        double dh = std::clamp(tgt, d.dhMin, d.dhMax);
                        dhs = {dh};
                        double up = std::clamp(tgt + 20, d.dhMin, d.dhMax);
                        if (std::fabs(up - dh) > 4 && !closing) dhs.push_back(up);
                        break;
                    }
                    case Kind::Homing: dhs = {std::clamp(tgt, d.dhMin, d.dhMax)}; break;
                    default: break;
                }
                int lip = 0, hole = 0, pad = 0;
                if (d.kind == Kind::Spring) {
                    pad = (int)std::ceil(d.param("pad", 8) / stepU_);
                    int land = (int)std::ceil(d.param("land", 16) / stepU_);
                    hole = n - pad - land;
                    if (hole < 3) continue;
                } else if (d.kind == Kind::Homing) {
                    lip = (int)std::ceil(d.param("lip", 8) / stepU_);
                    pad = (int)std::ceil(d.param("pad", 16) / stepU_);
                    double air = (n - lip - pad) * stepU_;
                    if (air < d.param("spacing", 32) * 1.5) continue;
                }
                for (double dh : dhs) {
                    if (closing && std::fabs(par.h + dh * mpu_ - hStart_) > 1e-6) continue;
                    tryPlace(idx, di, n, dh, lip, hole, pad);
                }
            }
        }
    }

    const Skeleton& sk_;
    const Profile& pr_;
    const ModuleLibrary& lib_;
    const GenParams& gp_;
    Rng& rng_;
    const AssembleOptions& opt_;
    int N_ = 0;
    double mpu_ = 0, stepU_ = 4, gapM_ = 0.3, hStart_ = 0.5;
    RangeMax rmq_;
    std::vector<int> partnerPrefix_;
    std::vector<double> chainZ_;  // heights along the expanded node's chain (stacked-layer checks)
    std::vector<Node> pool_;
    std::vector<std::vector<Node>> buckets_;
};

}  // namespace

bool assembleCourse(const Skeleton& sk, const Profile& pr, const ModuleLibrary& lib, const GenParams& gp, Rng& rng,
                    const AssembleOptions& opt, std::vector<Placed>& out, double* score) {
    if (sk.size() < 20) return false;
    Assembler a(sk, pr, lib, gp, rng, opt);
    return a.run(out, score);
}

bool assembleSafe(const Skeleton& sk, const Profile& pr, const ModuleLibrary& lib, const GenParams& gp,
                  std::vector<Placed>& out) {
    int N = sk.size();
    for (auto& p : sk.partners) if (!p.empty()) return false;
    int iStart = lib.findKind(Kind::Start), iTrack = lib.findKind(Kind::Track), iRail = lib.findKind(Kind::Rail);
    if (iStart < 0 || iTrack < 0 || iRail < 0) return false;
    const ModuleDef &dT = lib.defs[iTrack], &dR = lib.defs[iRail];
    double h = gp.prefMinH;
    for (double l : pr.L) h = std::max(h, l);
    if (h > gp.maxH) return false;
    double stepU = gp.stationU;
    int n0 = (int)std::ceil(lib.defs[iStart].lenMin / stepU);
    int tMin = (int)std::ceil(dT.lenMin / stepU), tMax = (int)std::floor(dT.lenMax / stepU);
    int rMin = (int)std::ceil(dR.lenMin / stepU), rMax = (int)std::floor(dR.lenMax / stepU);
    std::vector<uint8_t> needRail(N, 0);
    for (int i = 0; i < N; ++i) {
        if (std::fabs(sk.curv[i]) > dR.maxCurv) return false;
        if (std::fabs(sk.curv[i]) > dT.maxCurv)
            for (int k = -2; k <= 2; ++k) { int j = i + k; if (j >= n0 && j < N) needRail[j] = 1; }
    }
    // runs over [n0, N)
    struct Run { int a, b; bool rail; };
    std::vector<Run> runs;
    for (int i = n0; i < N;) {
        int j = i;
        while (j < N && needRail[j] == needRail[i]) ++j;
        runs.push_back({i, j, needRail[i] != 0});
        i = j;
    }
    // short track runs become rail; short rail runs absorb neighbours
    for (auto& r : runs) if (!r.rail && r.b - r.a < tMin) r.rail = true;
    std::vector<Run> merged;
    for (auto& r : runs) {
        if (!merged.empty() && merged.back().rail == r.rail) merged.back().b = r.b;
        else merged.push_back(r);
    }
    for (size_t k = 0; k < merged.size(); ++k) {
        Run& r = merged[k];
        if (!r.rail || r.b - r.a >= rMin) continue;
        int needMore = rMin - (r.b - r.a);
        if (k + 1 < merged.size()) {
            int take = std::min(needMore, merged[k + 1].b - merged[k + 1].a);
            r.b += take; merged[k + 1].a += take; needMore -= take;
        }
        if (needMore > 0 && k > 0) {
            int take = std::min(needMore, merged[k - 1].b - merged[k - 1].a);
            r.a -= take; merged[k - 1].b -= take; needMore -= take;
        }
        if (needMore > 0) return false;
    }
    std::vector<Run> fin;
    for (auto& r : merged) {
        if (r.b <= r.a) continue;
        if (!r.rail && r.b - r.a < tMin) {
            if (!fin.empty()) { fin.back().b = r.b; continue; }  // extend previous piece
            r.rail = true;
        }
        fin.push_back(r);
    }
    out.clear();
    Placed st;
    st.def = iStart; st.kind = Kind::Start; st.s0 = 0; st.s1 = n0; st.h0 = st.h1 = h;
    out.push_back(st);
    for (auto& r : fin) {
        int len = r.b - r.a;
        int mx = r.rail ? rMax : tMax, mn = r.rail ? rMin : tMin;
        int pieces = std::max(1, (len + mx - 1) / mx);
        if (len / pieces < mn) pieces = std::max(1, len / mn);
        int a = r.a;
        for (int p = 0; p < pieces; ++p) {
            int b = p + 1 == pieces ? r.b : a + len / pieces;
            Placed m;
            m.def = r.rail ? iRail : iTrack;
            m.kind = r.rail ? Kind::Rail : Kind::Track;
            m.s0 = a; m.s1 = b; m.h0 = m.h1 = h;
            out.push_back(m);
            a = b;
        }
    }
    return true;
}

}  // namespace rr
