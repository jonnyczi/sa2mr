#include "roomrun/profile.hpp"

#include <algorithm>
#include <cmath>

namespace rr {

static std::vector<double> smoothCyclic(const std::vector<double>& v, int halfWin) {
    int N = (int)v.size();
    std::vector<double> out(N);
    for (int i = 0; i < N; ++i) {
        double s = 0;
        for (int j = -halfWin; j <= halfWin; ++j) s += v[((i + j) % N + N) % N];
        out[i] = s / (2 * halfWin + 1);
    }
    return out;
}

Profile buildProfile(const Skeleton& sk, const Room& room, const GenParams& gp, Rng& rng) {
    const int N = sk.size();
    const double mpu = gp.metersPerUnit();
    const double hw = 0.5 * gp.trackWidthU * mpu;
    Profile pr;
    pr.L.assign(N, gp.minH);
    pr.U.assign(N, gp.maxH);
    pr.T.assign(N, 0.8);
    pr.box.assign(N, -1);
    for (int i = 0; i < N; ++i) {
        for (size_t b = 0; b < room.boxes.size(); ++b) {
            const Box& bx = room.boxes[b];
            if (bx.height > gp.tallBoxM) continue;  // obstacles are excluded by the free-space field
            if (bx.rect.dist(sk.pts[i]) < hw + gp.boxMarginM) {
                double top = bx.height + 0.01;
                if (top > pr.L[i]) { pr.L[i] = top; pr.box[i] = (int)b; }
            }
        }
    }
    if (sk.solved) {   // a solved stacked profile (coils): followed as it is, down to the window's floor
        for (int i = 0; i < N; ++i) pr.T[i] = std::clamp(sk.target[i], std::max(pr.L[i], gp.minH), pr.U[i]);
        return pr;
    }
    if (!sk.target.empty()) {
        pr.T = sk.target;
    } else {
        // rolling hills: two random sinusoids along arc length
        double L = sk.lengthM();
        int w1 = std::max(1, (int)std::round(L / rng.range(2.5, 4.5)));
        int w2 = std::max(2, (int)std::round(L / rng.range(1.2, 2.2)));
        double p1 = rng.range(0, 2 * kPi), p2 = rng.range(0, 2 * kPi);
        double base = rng.range(0.6, 0.95), a1 = rng.range(0.2, 0.45), a2 = rng.range(0.0, 0.15);   // up to the 1.6 m window
        for (int i = 0; i < N; ++i) {
            double t = 2 * kPi * i / N;
            pr.T[i] = base + a1 * std::sin(w1 * t + p1) + a2 * std::sin(w2 * t + p2);
        }
    }
    // visit furniture tops: target = top surface where the track crosses a box
    for (int i = 0; i < N; ++i)
        if (pr.box[i] >= 0) pr.T[i] = pr.L[i];
    // stacked layers (figure-8 crossings): first pass high, later pass low, both above furniture
    auto layerTargets = [&]() {
        if (!sk.target.empty()) return;
        const double gapM = gp.layerGapU * mpu + 0.05;
        for (int i = 0; i < N; ++i) {
            if (sk.partners[i].empty()) continue;
            bool first = true;
            double lmax = pr.L[i];
            for (int j : sk.partners[i]) { if (j < i) first = false; lmax = std::max(lmax, pr.L[j]); }
            double lo = std::max(lmax, gp.prefMinH + 0.05);
            pr.T[i] = first ? std::min(gp.maxH, std::max(gp.maxH - 0.12, lo + gapM)) : std::max(pr.L[i], gp.prefMinH + 0.05);
        }
    };
    layerTargets();
    int hwin = std::max(2, (int)(0.25 / sk.stepM));
    if (sk.target.empty()) pr.T = smoothCyclic(pr.T, hwin);
    layerTargets();
    // anticipate furniture: start climbing early enough to reach a top with rails/ramps
    std::vector<double> reach = pr.L;
    const double climb = 0.28 * sk.stepM;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = N - 1; i >= 0; --i) reach[i] = std::max(reach[i], reach[(i + 1) % N] - climb);
    for (int i = 0; i < N; ++i) pr.T[i] = std::max(pr.T[i], reach[i]);
    for (int i = 0; i < N; ++i) {
        if (pr.box[i] >= 0 && sk.partners[i].empty()) pr.T[i] = pr.L[i];
        pr.T[i] = std::clamp(pr.T[i], std::max(pr.L[i], gp.prefMinH), pr.U[i]);
    }
    // start line height must be a plain level section
    return pr;
}

}  // namespace rr
