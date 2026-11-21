// Beam-search assembler: fills a skeleton with library modules whose envelopes chain.
#pragma once
#include <vector>

#include "roomrun/course.hpp"
#include "roomrun/modules.hpp"
#include "roomrun/rng.hpp"

namespace rr {

struct Ban { int s0, s1; Kind kind; };  // a module of `kind` may not overlap [s0, s1)

struct AssembleOptions {
    std::vector<Ban> bans;
};

// Returns false if no closed assembly was found.
bool assembleCourse(const Skeleton& sk, const Profile& pr, const ModuleLibrary& lib, const GenParams& gp, Rng& rng,
                    const AssembleOptions& opt, std::vector<Placed>& out, double* score = nullptr);

// All-safe course: constant height, walled track + rails for tight turns only.
// Valid by construction (no speed requirements, no air time, no stacked layers).
bool assembleSafe(const Skeleton& sk, const Profile& pr, const ModuleLibrary& lib, const GenParams& gp,
                  std::vector<Placed>& out);

}  // namespace rr
