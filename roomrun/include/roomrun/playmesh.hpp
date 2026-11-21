// The playable mesh of a generated course (Room Run v0 in the game): the course made a continuous
// walled road that the real game's collision can carry, its colour groups for drawing in the
// room, and its centreline.
//
// v0 rules (see BRAINSTORM.md "Pillar B" and external/OpenSA2/host/xr/README.md "Room Run v0"):
//   * every station has a surface: springs and homing/gap flights become ramps bridging their two
//     ends (no spring or homing objects in the game yet); a step steeper than maxRiseSlope up
//     (maxFallSlope down; drops excepted), measured on the steepest deck facet, becomes a ramp;
//   * the profile is smoothed so its slope changes gradually (maxCreaseSlope a station) and the
//     walkway's width changes gently (widthRateU); the deck is in deckLanes lanes with matched
//     vertices (host/xr/README.md "Fixes from the Frame's third test");
//   * grind rails are a narrower walled strip; loops, dash panels and skill rails are painted road;
//   * walls of wallHeightU on both sides everywhere (collision, in pieces of their own: in the
//     deck's pieces the game sank Sonic into a sloped deck where he touched one), drawn as low
//     striped curbs; each piece's triangles sorted by Z, highest first (the game's collision cache);
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
    double maxRiseSlope = 0.45;   // steepest upward slope kept (rise per run of the walkway's shortest edge,
                                  // the inside of a bend); steeper is ramped
    double maxFallSlope = 0.8;    // steepest downward slope (the same run); a steeper one is spread forwards
    double dropSlope = 1.5;       // a downward step steeper than this is a drop (kept sharp, never smoothed)
    int smoothHalfStations = 2;   // the vertical profile's box filter: 2 h + 1 stations ...
    int smoothPasses = 4;         // ... this many times (about a Gaussian of 3 stations' sigma)
    double maxCreaseSlope = 0.08; // the profile's slope change from one station to the next (checked by the tests)
    int deckLanes = 2;            // the deck (and its underside) across its width, in collision and drawing
    double undersideU = 2.0;      // the collision underside (a ceiling) this far below the deck (units)
    double widthRateU = 1.0;      // the walkway's half width changes by at most this a station (units)
    double boxSinkM = 0.015;      // a deck set on a furniture top may sink this far into it (m) where it eases off its edge
    int chunkStations = 32;       // collision chunk length (stations)
    bool wallsApart = true;       // the walls in pieces of their own (the decks and undersides in others)
    int maxCollisionTriangles = 7000;   // fewer deck lanes above this (the game's collision pool holds 8191)
    bool sortForGame = true;      // each chunk's triangles by their middle's Z, highest first (the game's collision cache)
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
    uint8_t held = 0;      // the shaping held it up: 1 a furniture top, 2 a crossing's gap (diagnostics)
};

struct PlayChunk {
    int s0 = 0, s1 = 0;          // stations [s0, s1)
    std::vector<float> tri;      // 9 floats a triangle (a, b, c), metres, Y-up frame
    bool walls = false;          // a piece of walls only (PlayMeshParams::wallsApart)
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
    int lanes = 1;                // the deck lanes used (PlayMeshParams::deckLanes, or fewer on a long course)
    size_t collisionTriangles() const;
    size_t drawTriangles() const;
};

PlayMesh buildPlayMesh(const GenResult& r, const ModuleLibrary& lib, const GenParams& gp,
                       const PlayMeshParams& pp = PlayMeshParams());

// OBJ (Y-up, metres) of the drawn groups and, as object "collision", the collision chunks.
std::string playMeshToObj(const PlayMesh& m);

}  // namespace rr
