#include "main/game/status_names.h"

#include <cstdio>

#include "main/game/status_ids.h"
// Names of the common status kinds 0x00-0x111, in the order of
// Fighter::Status::Kind (lib/BrawlHeaders ft/fighter.h) with the underscores
// dropped. One string with NUL separators plus an offset table: about 3.8 KB.
namespace tm {
namespace game {
    static const char kNames[] =
        "Wait\0Walk\0WalkBrake\0Dash\0Run\0RunBrake\0Turn\0TurnDash\0TurnRun\0TurnRunBrake\0JumpSquat\0Jump\0JumpAerial\0"
        "Fly\0Fall\0FallAerial\0FallSpecial\0Squat\0SquatWait\0SquatF\0SquatB\0SquatRv\0Landing\0LandingLight\0"
        "LandingAttackAir\0LandingFallSpecial\0GuardOn\0Guard\0GuardOff\0GuardDamage\0Escape\0EscapeF\0EscapeB\0"
        "EscapeAir\0ReboundStop\0Rebound\0Attack\0Attack100\0AttackDash\0AttackS3\0AttackHi3\0AttackLw3\0AttackS4Start\0"
        "AttackS4Hold\0AttackS4\0AttackLw4Start\0AttackLw4Hold\0AttackLw4\0AttackHi4Start\0AttackHi4Hold\0AttackHi4\0"
        "AttackAir\0Catch\0CatchPull\0CatchDash\0CatchDashPull\0CatchTurn\0CatchWait\0CatchAttack\0CatchCut\0Throw\0"
        "CapturePulled\0CaptureWait\0CaptureDamage\0CaptureCut\0CaptureJump\0CaptureThrown\0Damage\0DamageAir\0"
        "DamageFly\0DamageFlyRoll\0DamageFlyReflectLr\0DamageFlyReflectU\0DamageFall\0Down\0DownSpot\0DownContinue\0"
        "DownWait\0DownWaitContinue\0DownEat\0LayDown\0DownStand\0DownStandFb\0DownStandAttack\0DownDamage\0"
        "DownReflectLr\0ShieldBreakFly\0ShieldBreakFall\0ShieldBreakDown\0FuraFuraStand\0FuraFura\0FuraFuraEnd\0"
        "DamageSongStart\0DamageSong\0DamageSongEnd\0Bind\0Passive\0PassiveFb\0PassiveWall\0PassiveWallJump\0"
        "PassiveCeil\0StopWall\0StopCeil\0WallJump\0AttachWall\0AttachWallWait\0DetachWall\0DetachWallFall\0"
        "DetachWallJump\0TreadJump\0TreadDamage\0TreadDamageRv\0TreadDamageAir\0TreadFall\0Pass\0CliffCatchMove\0"
        "CliffCatch\0CliffWait\0CliffAttack\0CliffClimb\0CliffEscape\0CliffJump1\0CliffJump2\0CliffJump3\0Ottoto\0"
        "OttotoWait\0MissFoot\0AirLasso\0AirLassoReach\0AirLassoHang\0AirLassoRewind\0AirLassoFailure\0GlideStart\0"
        "Glide\0GlideLanding\0GlideAttack\0GlideEnd\0Slip\0SlipDown\0SlipTurn\0SlipDash\0SlipWait\0SlipStand\0"
        "SlipStandAttack\0SlipStandF\0SlipStandB\0LadderCatch\0LadderCatchEnd\0LadderWait\0LadderEnd\0"
        "ItemPickupLight\0ItemPickupHeavy\0ItemLiftWait\0ItemLiftTurn\0ItemLiftWalk\0ItemThrow\0ItemThrowDash\0"
        "ItemThrowHeavy\0ItemSwing\0ItemSwingS3\0ItemSwingS4Start\0ItemSwingS4\0ItemSwingS4Hold\0ItemSwingDash\0"
        "ItemShootWait\0ItemShootWalkF\0ItemShootWalkBrakeF\0ItemShootWalkB\0ItemShootWalkBrakeB\0ItemShootDashF\0"
        "ItemShootDashB\0ItemShootJumpSquat\0ItemShootJump\0ItemShootAir\0ItemShootLanding\0ItemScrewJump\0"
        "ItemScrewJumpAerial\0ItemScrewFall\0ItemAssistHoist\0SwimDive\0SwimRise\0SwimUp\0SwimWait\0Swim\0SwimEnd\0"
        "SwimTurn\0SwimJump\0SwimDrown\0SwimDrownOut\0Dead\0Rebirth\0GimmickDoor\0GimmickBarrel\0GimmickCatapult\0"
        "GimmickTruck\0GimmickSpring\0GimmickSpringJump\0GimmickSpringBack\0GimmickEaten\0Bury\0BuryWait\0BuryJump\0"
        "Ice\0IceJump\0BittenWarioStart\0BittenWario\0BittenWarioEnd\0ClungCaptain\0Swallowed\0SwallowedDrink\0"
        "SwallowedCapture\0SwallowedThrownStar\0SwallowedThrown\0SwallowedCanceled\0ClungDiddy\0ClungDamageDiddy\0"
        "ClungThrownDiddy\0ShoulderedDonkeyStart\0ShoulderedDonkey\0ShoulderedDonkeyThrown\0Warpstar\0WarpstarJump\0"
        "DragoonRide\0HammerWait\0HammerWalk\0HammerTurn\0HammerJumpSquat\0HammerJump\0HammerFall\0HammerLanding\0"
        "ClungGanon\0CatchedGanon\0CatchedAirGanon\0CatchedAirFallGanon\0CatchedAirEndGanon\0CaptureYoshi\0YoshiEgg\0"
        "LinkFinal\0KoopaDived\0MetaknightFinal\0PitFall\0ThrowKirby\0Big\0Small\0HeartSwap\0HeartSwapEnd\0AdvFollow\0"
        "IkeFinalDamageFly\0IkeFinalDamage\0IkeFinalDamageFall\0CaptureMasterhand\0FinalVisualAttackOther\0"
        "CaptainFinalStart\0CaptainFinalFuraFura\0CaptainFinalClash\0CaptainFinalEnd\0KirbyFinalDamageFly\0"
        "KirbyFinalDamageWait\0CaptureGokulin\0CaptureCutGokulin\0CaptureTaboo\0CaptureBucyulus\0"
        "CaptureCutBucyulus\0CapturePulledYoshi\0CaptureWaitYoshi\0CaptureDamageYoshi\0CaptureCutYoshi\0Standby\0"
        "Appeal\0SmashAppeal\0Entry\0Win\0Lose\0TestMotion\0";

    static const u16 kOffsets[kCommonStatusCount] = {
        0, 5, 10, 20, 25, 29, 38, 43, 52, 60, 73, 83,
        88, 99, 103, 108, 119, 131, 137, 147, 154, 161, 169, 177,
        190, 207, 226, 234, 240, 249, 261, 268, 276, 284, 294, 306,
        314, 321, 331, 342, 351, 361, 371, 385, 398, 407, 422, 436,
        446, 461, 475, 485, 495, 501, 511, 521, 535, 545, 555, 567,
        576, 582, 596, 608, 622, 633, 645, 659, 666, 676, 686, 700,
        719, 737, 748, 753, 762, 775, 784, 801, 809, 817, 827, 839,
        855, 866, 880, 895, 911, 927, 941, 950, 962, 978, 989, 1003,
        1008, 1016, 1026, 1038, 1054, 1066, 1075, 1084, 1093, 1104, 1119, 1130,
        1145, 1160, 1170, 1182, 1196, 1211, 1221, 1226, 1241, 1252, 1262, 1274,
        1285, 1297, 1308, 1319, 1330, 1337, 1348, 1357, 1366, 1380, 1393, 1408,
        1424, 1435, 1441, 1454, 1466, 1475, 1480, 1489, 1498, 1507, 1516, 1526,
        1542, 1553, 1564, 1576, 1591, 1602, 1612, 1628, 1644, 1657, 1670, 1683,
        1693, 1707, 1722, 1732, 1744, 1761, 1773, 1789, 1803, 1817, 1832, 1852,
        1867, 1887, 1902, 1917, 1936, 1950, 1963, 1980, 1994, 2014, 2028, 2044,
        2053, 2062, 2069, 2078, 2083, 2091, 2100, 2109, 2119, 2132, 2137, 2145,
        2157, 2171, 2187, 2200, 2214, 2232, 2250, 2263, 2268, 2277, 2286, 2290,
        2298, 2315, 2327, 2342, 2355, 2365, 2380, 2397, 2417, 2433, 2451, 2462,
        2479, 2496, 2518, 2535, 2558, 2567, 2580, 2592, 2603, 2614, 2625, 2641,
        2652, 2663, 2677, 2688, 2701, 2717, 2737, 2756, 2769, 2778, 2788, 2799,
        2815, 2823, 2834, 2838, 2844, 2854, 2867, 2877, 2895, 2910, 2929, 2947,
        2970, 2988, 3009, 3027, 3043, 3063, 3084, 3099, 3117, 3130, 3146, 3165,
        3184, 3201, 3220, 3236, 3244, 3251, 3263, 3269, 3273, 3278
    };

    // Statuses from 0x112 are each fighter's own; the first five are shared
    // by every fighter (ProjectPunch's actions.h, MIT; see
    // THIRD_PARTY_NOTICES.md).
    static const char* const kSpecialNames[] = {"SpecialN", "SpecialS", "SpecialHi", "SpecialLw", "Final"};

    const char* statusName(int status)
    {
        if (status >= 0 && status < kCommonStatusCount)
        {
            return kNames + kOffsets[status];
        }
        if (status >= kStatusSpecialN && status <= kStatusFinal)
        {
            return kSpecialNames[status - kStatusSpecialN];
        }
        return NULL;
    }

    void statusText(int status, char* buf, u32 size)
    {
        const char* name = statusName(status);
        if (name != NULL)
        {
            snprintf(buf, size, "%s", name);
        }
        else
        {
            snprintf(buf, size, "0x%X", status);
        }
    }
}
}
