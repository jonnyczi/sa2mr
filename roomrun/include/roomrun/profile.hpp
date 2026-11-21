// Vertical profile: hard bounds (furniture tops, height window) and soft target heights.
#pragma once
#include <vector>

#include "roomrun/params.hpp"
#include "roomrun/rng.hpp"
#include "roomrun/room.hpp"
#include "roomrun/skeleton.hpp"

namespace rr {

struct Profile {
    std::vector<double> L, U, T;  // per-station lower bound, upper bound, target (m)
    std::vector<int> box;         // furniture under the track footprint (-1 none)
};

Profile buildProfile(const Skeleton& sk, const Room& room, const GenParams& gp, Rng& rng);

}  // namespace rr
