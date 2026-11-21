// The playable mesh of a generated course (Room Run v0 in the game): the course made a continuous
// walled road that the real game's collision can carry, its colour groups for drawing in the
// room, and its centreline.
//
// v0 rules (see BRAINSTORM.md "Pillar B" and external/OpenSA2/host/xr/README.md "Room Run v0"):
//   * every station has a surface: springs and homing/gap flights become ramps bridging their two
//     ends (no spring or homing objects in the game yet); a remaining upward step steeper than
//     maxRise is spread backwards into a ramp;
//   * grind rails are a narrower walled strip; loops, dash panels and skill rails are painted road;
//   * walls of wallHeightU on both sides everywhere (collision), drawn as low striped curbs;
//   * pillars from the deck down to the floor or a furniture top (drawn only).
//
// Frame: metres, X = the plan's x, Y = up, Z = -(the plan's y). That is a proper rotation of the
// plan's (x, y, z-up), so a triangle's winding keeps its meaning: (b - a) x (c - a) points out of
// the walkable side (up for decks, toward the track's middle for walls). The same frame as
// courseToObj.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "roomrun/generator.hpp"

namespace rr {

struct PlayMeshParams {
    double wallHeightU = 24.0;    // collision walls above the deck (units)
    double curbHeightU = 2.5;     // drawn curbs
    double curbWidthU = 1.6;
    double deckThickU = 2.0;      // drawn deck thickness
    double railHalfWidthU = 7.0;  // grind-rail strip half width (the road's is trackWidthU / 2)
    double maxRiseSlope = 0.45;   // steepest upward slope kept (rise per run); steeper is ramped
    int chunkStations = 32;       // collision chunk length (stations)
    double pillarEveryM = 0.36;
    double pillarHalfU = 1.3;
};

enum class PlayKind : uint8_t { Road, Ramp, Rail, Dash, Spring, Bridge, Loop, Start };
const char* playKindName(PlayKind k);

struct PlayStation {
    Vec3 p;              // the deck's surface at the centreline (Y-up frame, metres)
    Vec3 fwd;            // unit forward along the course (horizontal)
    double halfW = 0;    // the walkway's half width (metres)
    PlayKind kind = PlayKind::Road;
    bool bridged = false;  // a v0 bridge where the generator had no surface
};

struct PlayChunk {
    int s0 = 0, s1 = 0;          // stations [s0, s1)
    std::vector<float> tri;      // 9 floats a triangle (a, b, c), metres, Y-up frame
};

struct PlayGroup {
    std::string name;
    float rgb[3] = {1, 1, 1};
    std::vector<float> pos;      // 9 floats a triangle
    std::vector<float> nrm;      // the face normal, 3 floats a triangle
    size_t triangles() const { return pos.size() / 9; }
};

struct PlayMesh {
    std::vector<PlayStation> st;
    std::vector<PlayChunk> chunks;
    std::vector<PlayGroup> groups;
    double mpu = 0.012;
    int bridgedStations = 0, raisedStations = 0, railStations = 0;
    size_t collisionTriangles() const;
    size_t drawTriangles() const;
};

PlayMesh buildPlayMesh(const GenResult& r, const ModuleLibrary& lib, const GenParams& gp,
                       const PlayMeshParams& pp = PlayMeshParams());

// OBJ (Y-up, metres) of the drawn groups and, as object "collision", the collision chunks.
std::string playMeshToObj(const PlayMesh& m);

}  // namespace rr
