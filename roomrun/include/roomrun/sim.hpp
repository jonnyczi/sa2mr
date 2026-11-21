// Sonic-lite kinematic sim on the track spline + bot driver profiles.
#pragma once
#include <string>
#include <vector>

#include "roomrun/course.hpp"
#include "roomrun/modules.hpp"
#include "roomrun/rng.hpp"

namespace rr {

struct BotProfile {
    std::string name;
    double throttle = 1.0;                       // held stick magnitude
    double releaseRate = 0, releaseFrames = 0;   // random stick releases
    double brakeRate = 0, brakeFrames = 0;       // random brake taps
    double steerNoise = 0;                       // gaussian stick noise
    int reactionDelay = 0;                       // frames of lag on lateral correction
    double randomJumpRate = 0;                   // mashing jump on plain track
    int homingDelayMin = 2, homingDelayMax = 8;  // frames after bounce before homing press
    int jumpLeadMin = 1, jumpLeadMax = 3;        // frames before the lip the jump is pressed (<0 = late)
    double gapJumpProb = 0;                      // presses jump on run-off gaps (overshoot test)
    static BotProfile expert();
    static BotProfile average();
    static BotProfile bad();
};

enum class FailKind { None, Fell, Stuck, Timeout, NetLoop };
const char* failName(FailKind f);

struct PathSample { double s; Vec3 p; bool air; };  // p in units

struct RunResult {
    bool finished = false;
    FailKind fail = FailKind::None;
    int failModule = -1;
    std::vector<int> lapFrames;
    std::vector<int> netsPerModule;   // catch-net respawns, indexed by module
    std::vector<int> fallsPerModule;  // falls with no net under them (checkpoint restart)
    std::vector<PathSample> path;     // first lap, if recorded
    int totalFrames = 0;
    std::string detail;               // diagnostic for failures
};

// Simulates `laps` laps from a standing start at the start line.
RunResult simulateRun(const Course& c, const ModuleLibrary& lib, const GenParams& gp, const BotProfile& bot,
                      uint64_t seed, int laps, bool recordPath);

}  // namespace rr
