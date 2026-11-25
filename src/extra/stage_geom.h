#pragma once

#include <types.h>

#include "extra/stage_geom_math.h"
#include "main/game/fighters.h"

class Fighter;

// Live stage geometry queries (main floor edges, ledges, blast zones, ray
// probes) and clean fighter warp helpers for P+ 3.2.1.
namespace tm {
namespace extra {
    void stageGeomAttach();
    void stageGeomReset();

    // Reads live blast zones from CameraController + 0x158, falling back to
    // g_Stage->m_deadRange.
    bool readBlastZones(BlastZones& out);

    // Probes downward from (x, y) along (0, -maxDist). Returns true if a floor
    // surface (normal.y >= 0.5) was hit.
    bool rayCheckGround(float x, float y, float maxDist, float& hitY, bool passThrough);

    // Probes upward from (x, y) along (0, +dist) against solid ceilings.
    bool rayCheckCeiling(float x, float y, float dist = 150.0f);

    // Probes horizontally from (x, y) along (dirX * dist, 0) against solid walls.
    bool rayCheckWall(float x, float y, float dirX, float dist = 45.0f, float* hitDist = NULL);

    // Queries (with short per-match cache) the live stage's main floor, ledges,
    // blast zones, and (when includePlatforms is true) pass-through platforms
    // via stRayCheck + CameraController.
    bool queryStageGeometry(StageGeometry& out, bool forceRefresh = false, bool includePlatforms = false);
    bool liveGeometry(StageGeometry& out, bool forceRefresh = false, bool includePlatforms = false);

    // Rejection-samples a safe stage position verified by downward (0, -80)
    // and upward (0, +150) stRayCheck probes.
    bool findSafeRandomPoint(const StageGeometry& g, u32& rng, bool onGround, float& outX, float& outY);

    // Cleanly warps a fighter (and Ice Climbers partner if present), cancelling
    // hitstop, hitstun, and knockback velocity, optionally setting Correct_None
    // for noclip placement, applying damage if damage >= 0, and setting facing
    // if lr != 0.
    void placeStanding(const game::FighterRef& ref, float x, float y, bool setNoclip, float damage = -1.0f,
                       float lr = 0.0f);
    void placeFalling(const game::FighterRef& ref, float x, float y, bool setNoclip, float damage = -1.0f,
                      float lr = 0.0f);

    // Sets fighter (and Ice Climbers partner) facing direction (+1 right, -1 left).
    void setFighterFacing(const game::FighterRef& ref, float lr);

    // Sets soGroundModule::setCorrect on main fighter and partner (Correct_None
    // = 0, Correct_Air = 5).
    void setFighterCorrect(const game::FighterRef& ref, int correctKind);
    int getFighterCorrect(Fighter* f);
}
}
