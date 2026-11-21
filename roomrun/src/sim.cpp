#include "roomrun/sim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>

namespace rr {

BotProfile BotProfile::expert() {
    BotProfile b;
    b.name = "expert";
    b.jumpLeadMin = 1; b.jumpLeadMax = 3;
    b.homingDelayMin = 3; b.homingDelayMax = 8;
    return b;
}
BotProfile BotProfile::average() {
    BotProfile b;
    b.name = "average";
    b.throttle = 0.92;
    b.releaseRate = 0.003; b.releaseFrames = 25;
    b.brakeRate = 0.0005; b.brakeFrames = 10;
    b.steerNoise = 0.25; b.reactionDelay = 6;
    b.randomJumpRate = 0.0005;
    b.homingDelayMin = 5; b.homingDelayMax = 26;
    b.jumpLeadMin = 0; b.jumpLeadMax = 5;
    b.gapJumpProb = 0.2;
    return b;
}
BotProfile BotProfile::bad() {
    BotProfile b;
    b.name = "bad";
    b.throttle = 0.8;
    b.releaseRate = 0.006; b.releaseFrames = 45;
    b.brakeRate = 0.002; b.brakeFrames = 15;
    b.steerNoise = 0.6; b.reactionDelay = 14;
    b.randomJumpRate = 0.004;
    b.homingDelayMin = 6; b.homingDelayMax = 40;
    b.jumpLeadMin = -3; b.jumpLeadMax = 6;
    b.gapJumpProb = 0.4;
    return b;
}

const char* failName(FailKind f) {
    switch (f) {
        case FailKind::None: return "none";
        case FailKind::Fell: return "fell";
        case FailKind::Stuck: return "stuck";
        case FailKind::Timeout: return "timeout";
        case FailKind::NetLoop: return "net-loop";
    }
    return "?";
}

namespace {

struct Track {
    int N = 0;
    double step = 4, L = 0;
    std::vector<Vec2> P, T, Nm;
    std::vector<double> k, z, floorRef;  // floorRef: surface, or lower end of an air section
    const Course* c = nullptr;
    int idx(double s) const {
        long j = (long)std::floor(s / step);
        j %= N;
        if (j < 0) j += N;
        return (int)j;
    }
    double localS(double s) const { double r = std::fmod(s, L); return r < 0 ? r + L : r; }
    Vec2 pos(double s, double d) const {
        int j = idx(s), jn = (j + 1) % N;
        double f = localS(s) / step - std::floor(localS(s) / step);
        Vec2 p = P[j] + (P[jn] - P[j]) * f;
        Vec2 n = (Nm[j] * (1 - f) + Nm[jn] * f).norm();
        return p + n * d;
    }
    double surf(double s) const {
        int j = idx(s), jn = (j + 1) % N;
        double f = localS(s) / step - std::floor(localS(s) / step);
        if (c->exists[j] && c->exists[jn] && std::fabs(z[jn] - z[j]) < 3.5) return z[j] + (z[jn] - z[j]) * f;
        return z[j];
    }
};

enum class Mode { Ground, Air, Spring, Loop, Homing };

double gauss(Rng& r) {
    double u1 = std::max(1e-12, r.uniform()), u2 = r.uniform();
    return std::sqrt(-2 * std::log(u1)) * std::cos(2 * kPi * u2);
}

class Runner {
public:
    Runner(const Course& c, const ModuleLibrary& lib, const GenParams& gp, const BotProfile& bot, uint64_t seed)
        : c_(c), lib_(lib), gp_(gp), bot_(bot), rng_(seed), sp_(gp.physics, gp.speedScale), p_(sp_.p) {
        double mpu = gp.metersPerUnit();
        tr_.c = &c;
        tr_.N = c.sk.size();
        tr_.step = gp.stationU;
        tr_.L = tr_.N * tr_.step;
        tr_.P.resize(tr_.N); tr_.T.resize(tr_.N); tr_.Nm.resize(tr_.N); tr_.z.resize(tr_.N);
        for (int j = 0; j < tr_.N; ++j) { tr_.P[j] = c.sk.pts[j] / mpu; tr_.z[j] = c.z[j] / mpu; }
        for (int j = 0; j < tr_.N; ++j) {
            tr_.T[j] = (tr_.P[(j + 1) % tr_.N] - tr_.P[(j + tr_.N - 1) % tr_.N]).norm();
            tr_.Nm[j] = tr_.T[j].perp();
        }
        tr_.k = c.sk.curv;
        tr_.floorRef.resize(tr_.N);
        for (int j = 0; j < tr_.N; ++j) {
            const Placed& m = c.mods[c.mod[j]];
            tr_.floorRef[j] = c.exists[j] ? tr_.z[j] : std::min(m.h0, m.h1) / mpu;
        }
        hw_ = gp.trackWidthU * 0.5;
        wallD_ = hw_ - gp.physics.sonicRadius;
    }

    RunResult run(int laps, bool record) {
        RunResult rr;
        rr.netsPerModule.assign(c_.mods.size(), 0);
        rr.fallsPerModule.assign(c_.mods.size(), 0);
        res_ = &rr;
        record_ = record;
        const long maxFrames = (long)(laps * gp_.maxLapSeconds * 60);
        long frame = 0, lastProgressFrame = 0;
        double lastProgressS = 0, maxS = 0;
        int lapsDone = 0;
        long lapStart = 0;
        s_ = 0; d_ = 0; v_ = 0; vd_ = 0;
        mode_ = Mode::Ground;
        curMod_ = 0;
        while (frame < maxFrames) {
            ++frame;
            step();
            frame += penalty_;
            penalty_ = 0;
            if (fail_ != FailKind::None) break;
            if (s_ >= (lapsDone + 1) * tr_.L) {
                rr.lapFrames.push_back((int)(frame - lapStart));
                lapStart = frame;
                ++lapsDone;
                lapNets_ = 0;
                record_ = false;
                if (lapsDone >= laps) { rr.finished = true; break; }
            }
            maxS = std::max(maxS, s_);
            if (frame - lastProgressFrame >= 900) {
                if (maxS - lastProgressS < 40) {
                    fail_ = FailKind::Stuck; failMod_ = modAt(s_);
                    char b[160];
                    std::snprintf(b, sizeof b, "mode=%d s=%.1f v=%.2f d=%.1f st=%d kind=%s spA=%.1f spB=%.1f spT=%d spF=%d mod=%d", (int)mode_, tr_.localS(s_), v_, d_, tr_.idx(s_), kindName(c_.mods[failMod_].kind), spA_, spB_, spT_, spF_, failMod_);
                    rr.detail += std::string(" STUCK[") + b + "]";
                    break;
                }
                lastProgressFrame = frame;
                lastProgressS = maxS;
            }
        }
        if (!rr.finished && fail_ == FailKind::None) { fail_ = FailKind::Timeout; failMod_ = modAt(s_); }
        rr.fail = fail_;
        rr.failModule = failMod_;
        rr.totalFrames = (int)frame;
        return rr;
    }

private:
    int modAt(double s) const { return c_.mod[tr_.idx(s)]; }
    const ModuleDef& defOf(int mi) const { return lib_.defs[c_.mods[mi].def]; }

    void sample(Vec3 p, bool air) {
        if (record_) res_->path.push_back({tr_.localS(s_), p, air});
    }

    // ------------------------------------------------------------ bot input
    void decide(double& throttle, double& steer) {
        throttle = bot_.throttle;
        if (releaseLeft_ > 0) { --releaseLeft_; throttle = 0; }
        else if (brakeLeft_ > 0) { --brakeLeft_; throttle = -1; }
        else if (rng_.chance(bot_.releaseRate)) releaseLeft_ = (int)bot_.releaseFrames;
        else if (rng_.chance(bot_.brakeRate)) brakeLeft_ = (int)bot_.brakeFrames;
        dHist_.push_back(d_);
        while ((int)dHist_.size() > bot_.reactionDelay + 1) dHist_.pop_front();
        double dSeen = dHist_.front();
        double kap = tr_.k[tr_.idx(s_)];
        double target = bot_.name == "expert" ? (kap > 0 ? 1 : -1) * std::min(1.0, std::fabs(kap) * 40) * 0.3 * hw_ : 0.0;
        steer = std::clamp(0.15 * (target - dSeen) - 1.5 * vd_, -1.0, 1.0);
        if (bot_.steerNoise > 0) steer = std::clamp(steer + bot_.steerNoise * gauss(rng_) * 0.5, -1.0, 1.0);
    }

    template <class... A>
    void trace(const char* fmt, A... a) {
        if (!gp_.debug || res_->detail.size() > 600) return;
        char b[128];
        if constexpr (sizeof...(A) == 0) std::snprintf(b, sizeof b, "%s", fmt);
        else std::snprintf(b, sizeof b, fmt, a...);
        res_->detail += " ";
        res_->detail += b;
    }

    // ------------------------------------------------------------ events
    void respawn(int mi, bool net = true) {
        if (mi < 0) mi = 0;
        if (net) res_->netsPerModule[mi]++;
        if (gp_.debug && res_->detail.size() < 600) {
            char b[96];
            std::snprintf(b, sizeof b, " R(%d:%s %s s=%.0f z=%.0f mode=%d)", mi, kindName(c_.mods[mi].kind), net ? "net" : "fall", tr_.localS(s_), ap_.z, (int)mode_);
            res_->detail += b;
        }
        if (++lapNets_ > 8) { fail_ = FailKind::NetLoop; failMod_ = mi; return; }
        int cp = std::max(0, mi - 1);
        double lapBase = std::floor(s_ / tr_.L) * tr_.L;
        double sLocal = c_.mods[cp].s0 * tr_.step;
        if (sLocal > tr_.localS(s_) + tr_.L * 0.5) lapBase -= tr_.L;  // checkpoint lies in previous lap
        s_ = lapBase + sLocal + 0.01;
        v_ = 0; d_ = 0; vd_ = 0;
        mode_ = Mode::Ground;
        curMod_ = -1;
        lock_ = 0;
        penalty_ = 60;
    }

    void goAirborne(double vz, int fromMod) {
        Vec2 xy = tr_.pos(s_, d_);
        int j = tr_.idx(s_);
        ap_ = {xy.x, xy.y, tr_.surf(s_)};
        Vec2 hv = tr_.T[j] * v_ + tr_.Nm[j] * vd_;
        av_ = {hv.x, hv.y, vz};
        ja_ = j;
        airMod_ = fromMod;
        airJump_ = vz >= p_.jumpV * 0.9;
        lastNetMod_ = -1;
        lastExposedMod_ = -1;
        mode_ = Mode::Air;
    }

    // Called when Sonic's ground position enters module mi. Returns true if mode changed.
    bool onEnter(int mi) {
        curMod_ = mi;
        const Placed& m = c_.mods[mi];
        const ModuleDef& d = defOf(mi);
        switch (m.kind) {
            case Kind::Dash:
                v_ = d.param("speed", 5);
                lock_ = (int)(d.param("lock", 30) / sp_.k);
                d_ *= 0.3; vd_ = 0;
                return false;
            case Kind::Rail:
                d_ = 0; vd_ = 0;
                v_ = std::max(v_, d.param("minSpeed", 1.8));
                return false;
            case Kind::Gap: case Kind::Homing: {
                int lead = rng_.irange(bot_.jumpLeadMin, bot_.jumpLeadMax);
                jumpInTime_ = lead >= 0 && (m.kind == Kind::Homing || rng_.chance(bot_.gapJumpProb));
                return false;
            }
            case Kind::Spring: {
                int landN = m.n() - m.padN - m.holeN;
                spA_ = s_;
                spB_ = std::floor(s_ / tr_.L) * tr_.L + (m.s1 - landN) * tr_.step + 1.0;
                if (spB_ < spA_) spB_ += tr_.L;
                double mpu = gp_.metersPerUnit();
                spZA_ = m.h0 / mpu; spZB_ = m.h1 / mpu;
                double A = std::max(spZA_, spZB_) + d.param("apex", 16), g = p_.gravity;
                spTu_ = std::sqrt(2 * (A - spZA_) / g);
                spF_ = std::max(2, (int)std::ceil(spTu_ + std::sqrt(2 * (A - spZB_) / g)));
                spT_ = 0;
                mode_ = Mode::Spring;
                return true;
            }
            case Kind::Loop:
                loopV0_ = std::max(v_, d.param("speed", 6));
                loopTheta_ = 0;
                loopR_ = d.param("radius", 22);
                mode_ = Mode::Loop;
                return true;
            default: return false;
        }
    }

    bool startHoming(int mi, bool fromAir = false) {
        const Placed& m = c_.mods[mi];
        const ModuleDef& d = defOf(mi);
        double mpu = gp_.metersPerUnit();
        targets_.clear();
        double sBase = std::floor(s_ / tr_.L) * tr_.L;
        double a = (m.s0 + m.lipN) * tr_.step, b = (m.s1 - m.padN) * tr_.step;
        int K = std::max(1, (int)((b - a) / d.param("spacing", 32)));
        for (int k = 0; k < K; ++k) {
            double s = a + (b - a) * (k + 0.5) / K;
            double f = (s - a) / (b - a);
            if (fromAir && s < tr_.localS(s_) + 6) continue;
            Vec2 xy = tr_.pos(sBase + s, 0);
            targets_.push_back({xy.x, xy.y, (m.h0 + (m.h1 - m.h0) * f) / mpu + d.param("lift", 12)});
        }
        padS_ = sBase + (m.s1 - m.padN * 0.5) * tr_.step;
        padZ_ = m.h1 / mpu;
        homingMod_ = mi;
        hk_ = 0; hPhase_ = 0; hWait_ = 0;
        hDelay_ = rng_.irange(bot_.homingDelayMin, bot_.homingDelayMax);
        hWindow_ = (int)(d.param("window", 40) / sp_.k * 0.6);
        hSpeed_ = d.param("speed", 4.2);
        if (targets_.empty()) return false;
        if (!fromAir) goAirborne(p_.jumpV, mi);
        airMod_ = mi;
        mode_ = Mode::Homing;
        return true;
    }

    // ------------------------------------------------------------ per-frame
    void step() {
        switch (mode_) {
            case Mode::Ground: groundStep(); break;
            case Mode::Air: airStep(); break;
            case Mode::Spring: springStep(); break;
            case Mode::Loop: loopStep(); break;
            case Mode::Homing: homingStep(); break;
        }
    }

    void groundStep() {
        int j = tr_.idx(s_);
        int mi = c_.mod[j];
        if (mi != curMod_ && onEnter(mi)) return;
        const Placed& m = c_.mods[mi];
        const ModuleDef& d = defOf(mi);
        bool rail = c_.rail[j];
        double throttle, steer;
        decide(throttle, steer);
        if (lock_ > 0) { --lock_; throttle = 1; steer = 0; }
        int jn = (j + 1) % tr_.N;
        double slope = (c_.exists[jn] && std::fabs(tr_.z[jn] - tr_.z[j]) < 3.5) ? (tr_.z[jn] - tr_.z[j]) / tr_.step : 0;
        double a;
        if (rail) {
            a = -d.param("friction", 0.003) * sp_.k * sp_.k;
        } else if (throttle > 0) {
            a = v_ < p_.runMax * throttle ? p_.runAccel * throttle : -p_.dragAbove;
            if (v_ > p_.runMax && v_ - p_.dragAbove < p_.runMax) a = -(v_ - p_.runMax);
            if (v_ < p_.runMax * throttle && v_ + a > p_.runMax * throttle) a = p_.runMax * throttle - v_;
        } else if (throttle == 0) {
            a = -std::min(p_.coastDecel, std::max(0.0, v_));
        } else {
            a = -std::min(p_.brakeDecel, std::max(0.0, v_));
        }
        v_ += a - p_.gravity * p_.slopeFactor * slope;
        if (rail) v_ = std::max(v_, d.param("minSpeed", 1.8));
        if (d.param("speedCap", 0) > 0) v_ = std::min(v_, d.param("speedCap", 0));  // water, climbing, ...
        v_ = std::clamp(v_, -2.0, p_.topSpeed);
        // lateral: centrifugal drift beyond grip, stick correction, walls
        if (rail) { d_ = 0; vd_ = 0; }
        else {
            double kap = tr_.k[j];
            double ac = v_ * v_ * std::fabs(kap);
            double drift = ac > p_.grip ? -(kap > 0 ? 1 : -1) * (ac - p_.grip) : 0;
            vd_ = (vd_ + drift + steer * p_.steerAccel) * p_.latDamp;
            d_ += vd_;
            if (c_.walled[j]) {
                if (std::fabs(d_) > wallD_) { d_ = std::clamp(d_, -wallD_, wallD_); vd_ = 0; v_ *= p_.wallScrape; }
            } else if (std::fabs(d_) > hw_) {
                goAirborne(0, mi);  // ran off an unwalled edge
                return;
            }
        }
        // random jump (mashing) on plain track
        if (!rail && lock_ == 0 && (m.kind == Kind::Track || m.kind == Kind::Start || m.kind == Kind::Ramp) &&
            rng_.chance(bot_.randomJumpRate)) {
            goAirborne(p_.jumpV + v_ * slope, mi);
            return;
        }
        // advance, checking every station boundary crossed this frame
        double sNew = s_ + v_;
        if (v_ > 0) {
            double b = (std::floor(s_ / tr_.step) + 1) * tr_.step;
            int jPrev = j;
            while (b <= sNew) {
                int jb = tr_.idx(b);
                bool leave = !c_.exists[jb] || (!c_.rail[jb] && tr_.z[jb] < tr_.z[jPrev] - 3.0);
                if (leave) {
                    s_ = b - 0.01;
                    int lm = c_.mod[jPrev];
                    const Placed& lpm = c_.mods[lm];
                    if (lpm.kind == Kind::Homing) {
                        if (jumpInTime_) startHoming(lm); else { trace("H(late jump)"); goAirborne(v_ * slope, lm); }
                    } else {
                        double vz = v_ * slope;
                        if (lpm.kind == Kind::Gap) {
                            const ModuleDef& gd = defOf(lm);
                            double ang = gd.param("kickAngle", 6) * kPi / 180;
                            vz = v_ * std::sin(ang);
                            v_ *= std::cos(ang);
                            if (jumpInTime_) vz += p_.jumpV;
                        }
                        goAirborne(vz, lm);
                    }
                    return;
                }
                if (c_.mod[jb] != curMod_) {
                    s_ = b + 0.01;
                    if (onEnter(c_.mod[jb])) return;
                }
                jPrev = jb;
                b += tr_.step;
            }
        }
        s_ = sNew;
        Vec2 xy = tr_.pos(s_, d_);
        sample({xy.x, xy.y, tr_.surf(s_)}, false);
    }

    // project air position onto the track near ja_; updates s_, d_
    void project(double& zSurf, int& jj) {
        int best = ja_;
        double bd = 1e30;
        int ahead = 4 + (int)(std::hypot(av_.x, av_.y) / tr_.step) * 2;
        for (int k = -3; k <= ahead; ++k) {
            int j = ((ja_ + k) % tr_.N + tr_.N) % tr_.N;
            double dd = (Vec2{ap_.x, ap_.y} - tr_.P[j]).len2();
            if (dd < bd) { bd = dd; best = j; }
        }
        ja_ = best;
        jj = best;
        Vec2 rel = Vec2{ap_.x, ap_.y} - tr_.P[best];
        double sl = best * tr_.step + rel.dot(tr_.T[best]);
        double cur = tr_.localS(s_);
        double delta = sl - cur;
        if (delta > tr_.L / 2) delta -= tr_.L;
        if (delta < -tr_.L / 2) delta += tr_.L;
        s_ += delta;
        d_ = rel.dot(tr_.Nm[best]);
        zSurf = tr_.z[best];
    }

    // shared ballistic integration; returns true if landed
    bool airPhysics(double steer, bool allowLand) {
        double prevZ = ap_.z;
        av_.z -= p_.gravity;
        int j0 = ja_;
        av_.x += tr_.Nm[j0].x * steer * p_.steerAccel * 0.3;
        av_.y += tr_.Nm[j0].y * steer * p_.steerAccel * 0.3;
        ap_ = ap_ + av_;
        double zs; int jj;
        project(zs, jj);
        if (allowLand) {  // object contacts while airborne
            int mj = c_.mod[jj];
            const Placed& pm = c_.mods[mj];
            if (pm.kind == Kind::Spring && mj != curMod_ && jj - pm.s0 <= pm.padN && ap_.z >= zs - 2 && ap_.z - zs < 25) {
                onEnter(mj);  // touched the spring pad
                return true;
            }
            if (pm.kind == Kind::Homing && airJump_ && startHoming(mj, true)) return true;
        }
        // invisible walls on walled sections
        if (c_.walled[jj] && std::fabs(d_) > wallD_ && ap_.z < zs + 40) {
            double excess = d_ - std::clamp(d_, -wallD_, wallD_);
            ap_.x -= tr_.Nm[jj].x * excess; ap_.y -= tr_.Nm[jj].y * excess;
            double lat = av_.x * tr_.Nm[jj].x + av_.y * tr_.Nm[jj].y;
            av_.x -= tr_.Nm[jj].x * lat; av_.y -= tr_.Nm[jj].y * lat;
            d_ = std::clamp(d_, -wallD_, wallD_);
        }
        sample(ap_, true);
        (void)prevZ;
        if (allowLand && c_.exists[jj] && ap_.z <= zs && ap_.z >= zs - 8 && std::fabs(d_) <= hw_ + 1) {
            mode_ = Mode::Ground;
            v_ = av_.x * tr_.T[jj].x + av_.y * tr_.T[jj].y;
            vd_ = 0;
            if (c_.rail[jj]) d_ = 0;
            curMod_ = c_.mod[jj];
            if (c_.mods[curMod_].kind == Kind::Rail) v_ = std::max(v_, defOf(curMod_).param("minSpeed", 1.8));
            return true;
        }
        if (!c_.exists[jj] && defOf(c_.mod[jj]).net) lastNetMod_ = c_.mod[jj];
        if (!c_.walled[jj]) lastExposedMod_ = c_.mod[jj];
        if (ap_.z < tr_.floorRef[jj] - 40 || ap_.z < -200) {
            int mi = c_.mod[jj];
            if (defOf(mi).net) respawn(mi);  // nets span the whole set-piece footprint
            else if (airMod_ >= 0 && defOf(airMod_).net) respawn(airMod_);
            else if (lastNetMod_ >= 0) respawn(lastNetMod_);
            else {
                // no net: in MR this is a fall to the real floor -> checkpoint restart (flagged for repair)
                int fm = lastExposedMod_ >= 0 ? lastExposedMod_ : mi;  // last unwalled section flown over
                res_->fallsPerModule[fm]++;
                char b[200];
                std::snprintf(b, sizeof b, "st=%d exists=%d walled=%d z=%.1f zs=%.1f d=%.1f airMod=%d(%s) stMod=%d(%s)", jj, c_.exists[jj], c_.walled[jj], ap_.z, zs, d_, airMod_, airMod_ >= 0 ? kindName(c_.mods[airMod_].kind) : "-", mi, kindName(c_.mods[mi].kind));
                if (gp_.debug && res_->detail.size() < 600) res_->detail += std::string(" F[") + b + "]";
                respawn(fm, false);
            }
        }
        return false;
    }

    void airStep() {
        double throttle, steer;
        decide(throttle, steer);
        airPhysics(steer, true);
    }

    void springStep() {
        ++spT_;
        double g = p_.gravity;
        double t = std::min((double)spT_, (double)spF_) * (spTu_ + std::sqrt(2 * (std::max(spZA_, spZB_) + defOf(curMod_).param("apex", 16) - spZB_) / g)) / spF_;
        double z = spZA_ + g * spTu_ * t - 0.5 * g * t * t;
        s_ = spA_ + (spB_ - spA_) * std::min(1.0, (double)spT_ / spF_);
        d_ *= 0.8;
        Vec2 xy = tr_.pos(s_, d_);
        sample({xy.x, xy.y, z}, true);
        if (spT_ >= spF_) {
            mode_ = Mode::Ground;
            s_ = spB_;
            v_ = defOf(curMod_).param("exitSpeed", 2.0);
            d_ = 0; vd_ = 0;
            lock_ = 8;
        }
    }

    void loopStep() {
        double z = loopR_ * (1 - std::cos(loopTheta_));
        double v2 = loopV0_ * loopV0_ - 2 * p_.gravity * z;
        if (v2 <= 0 || v2 / loopR_ + p_.gravity * std::cos(loopTheta_) < 0) {
            // fell off the loop: drop back to its entry, speed lost
            res_->netsPerModule[curMod_]++;
            mode_ = Mode::Ground;
            v_ = 0;
            penalty_ = 30;
            return;
        }
        double v = std::sqrt(v2);
        loopTheta_ += v / loopR_;
        Vec2 xy = tr_.pos(s_ + loopR_ * std::sin(loopTheta_), 0);
        sample({xy.x, xy.y, tr_.surf(s_) + z}, true);
        if (loopTheta_ >= 2 * kPi) {
            mode_ = Mode::Ground;
            v_ = loopV0_;
        }
    }

    void homingStep() {
        if (hPhase_ == 0) {
            ++hWait_;
            if (hWait_ > hWindow_ && hDelay_ > hWindow_) { trace("H(miss k=%d)", hk_); mode_ = Mode::Air; airJump_ = false; airStep(); return; }  // missed press
            airPhysics(0, false);
            if (fail_ != FailKind::None || mode_ != Mode::Homing) return;
            if (hWait_ >= hDelay_) hPhase_ = 1;
            return;
        }
        Vec3 tgt = targets_[hk_];
        Vec3 dv = tgt - ap_;
        double dist = dv.len();
        if (dist <= hSpeed_) {
            ap_ = tgt;
            ++hk_;
            int j = ja_;
            if (hk_ >= (int)targets_.size()) {
                double vz = p_.jumpV * 0.9;
                double drop = ap_.z - padZ_;
                double ta = (vz + std::sqrt(vz * vz + 2 * p_.gravity * std::max(0.0, drop))) / p_.gravity;
                double cur = s_;
                double dsPad = padS_ - cur;
                if (dsPad < -tr_.L / 2) dsPad += tr_.L;
                double vh = std::max(0.3, dsPad / ta);
                av_ = {tr_.T[j].x * vh, tr_.T[j].y * vh, vz};
                mode_ = Mode::Air;
                airMod_ = homingMod_;
                airJump_ = false;
                return;
            }
            av_ = {tr_.T[j].x * 0.4, tr_.T[j].y * 0.4, p_.jumpV * 0.9};
            hPhase_ = 0; hWait_ = 0;
            hDelay_ = rng_.irange(bot_.homingDelayMin, bot_.homingDelayMax);
        } else {
            ap_ = ap_ + dv * (hSpeed_ / dist);
            av_ = dv * (hSpeed_ / dist);
        }
        double zs; int jj;
        project(zs, jj);
        sample(ap_, true);
    }

    const Course& c_;
    const ModuleLibrary& lib_;
    const GenParams& gp_;
    BotProfile bot_;
    Rng rng_;
    ScaledPhysics sp_;
    const Physics& p_;
    Track tr_;
    double hw_ = 12, wallD_ = 9;
    RunResult* res_ = nullptr;
    bool record_ = false;
    // state
    Mode mode_ = Mode::Ground;
    double s_ = 0, d_ = 0, v_ = 0, vd_ = 0;
    int curMod_ = 0, lock_ = 0, penalty_ = 0, lapNets_ = 0;
    FailKind fail_ = FailKind::None;
    int failMod_ = -1;
    int releaseLeft_ = 0, brakeLeft_ = 0;
    std::deque<double> dHist_;
    bool jumpInTime_ = true;
    Vec3 ap_, av_;
    int ja_ = 0, airMod_ = -1, lastNetMod_ = -1, lastExposedMod_ = -1;
    bool airJump_ = false;
    // spring
    double spA_ = 0, spB_ = 0, spZA_ = 0, spZB_ = 0, spTu_ = 0;
    int spF_ = 1, spT_ = 0;
    // loop
    double loopV0_ = 0, loopTheta_ = 0, loopR_ = 22;
    // homing
    std::vector<Vec3> targets_;
    double padS_ = 0, padZ_ = 0, hSpeed_ = 4;
    int hk_ = 0, hPhase_ = 0, hWait_ = 0, hDelay_ = 0, hWindow_ = 24, homingMod_ = -1;
};

}  // namespace

RunResult simulateRun(const Course& c, const ModuleLibrary& lib, const GenParams& gp, const BotProfile& bot,
                      uint64_t seed, int laps, bool recordPath) {
    Runner r(c, lib, gp, bot, seed);
    return r.run(laps, recordPath);
}

}  // namespace rr
