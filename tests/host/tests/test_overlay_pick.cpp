// Colour overlays: which group applies and when Play Sound sounds
// (game::overlayGroup and game::overlaySoundDue, src/main/game/overlays.h).
// Not covered: the groups' own tests on the fighter and the colours (make
// test-osds), Show Collision's drawing and the sound itself (make test-display).
#include <string.h>

#include "host/test.h"
#include "main/game/overlays.h"
#include "main/game/tech_frames.h"

using namespace tm;
using namespace tm::game;

namespace {
    u8 s_groups[kOverlayGroupCount];

    void clearGroups()
    {
        memset(s_groups, kOverlayNone, sizeof(s_groups));
    }

    u32 bit(int g)
    {
        return 1u << g;
    }

    // One step: the group that applies with `active`, and whether Play Sound
    // sounds, given what applied before (`group`, updated).
    bool step(int& group, u32 active)
    {
        int now = overlayGroup(s_groups, active);
        bool due = overlaySoundDue(group, now, s_groups);
        group = now;
        return due;
    }
}

TEST(overlay_pick, the_last_active_group_with_a_choice_applies)
{
    clearGroups();
    CHECK_EQ(-1, overlayGroup(s_groups, 0xFFFFFFFFu));
    s_groups[kGroupActionable] = kOverlayRed;
    s_groups[kGroupWait] = kOverlayCollision;
    s_groups[kGroupIasa] = kOverlayNone;
    CHECK_EQ((int)kGroupWait, overlayGroup(s_groups, bit(kGroupActionable) | bit(kGroupWait) | bit(kGroupIasa)));
    CHECK_EQ((int)kGroupActionable, overlayGroup(s_groups, bit(kGroupActionable) | bit(kGroupIasa)));
    CHECK_EQ(-1, overlayGroup(s_groups, bit(kGroupIasa)));
    // Show Collision and Play Sound win like any other non-None value.
    s_groups[kGroupShieldStun] = kOverlaySound;
    CHECK_EQ((int)kGroupShieldStun, overlayGroup(s_groups, bit(kGroupWait) | bit(kGroupShieldStun)));
    // A value past the table is ignored.
    s_groups[kGroupShieldStun] = kOverlayChoiceCount;
    CHECK_EQ((int)kGroupWait, overlayGroup(s_groups, bit(kGroupWait) | bit(kGroupShieldStun)));
}

TEST(overlay_pick, play_sound_sounds_once_per_entry)
{
    clearGroups();
    s_groups[kGroupWait] = kOverlaySound;
    s_groups[kGroupDash] = kOverlayRed;
    int group = -1;
    CHECK(!step(group, 0));
    CHECK(step(group, bit(kGroupWait)));  // enters Wait
    CHECK(!step(group, bit(kGroupWait))); // stays
    CHECK(!step(group, bit(kGroupWait)));
    CHECK(!step(group, 0));               // leaves
    CHECK(step(group, bit(kGroupWait)));  // enters again
    CHECK(!step(group, bit(kGroupDash))); // another group applies
    CHECK(step(group, bit(kGroupWait)));  // back: one-shot compares the winning group
}

TEST(overlay_pick, a_more_specific_sound_group_sounds_again_and_so_does_returning)
{
    clearGroups();
    s_groups[kGroupActionable] = kOverlaySound;
    s_groups[kGroupIasa] = kOverlaySound;
    int group = -1;
    CHECK(step(group, bit(kGroupActionable)));
    CHECK(step(group, bit(kGroupActionable) | bit(kGroupIasa)));
    CHECK(!step(group, bit(kGroupActionable) | bit(kGroupIasa)));
    CHECK(step(group, bit(kGroupActionable)));
}

TEST(overlay_pick, a_sound_group_under_a_more_specific_one_is_silent)
{
    clearGroups();
    s_groups[kGroupActionable] = kOverlaySound;
    s_groups[kGroupWait] = kOverlayGreen;
    int group = -1;
    CHECK(!step(group, bit(kGroupActionable) | bit(kGroupWait)));
    CHECK(step(group, bit(kGroupActionable)));
}

TEST(tech_frames, distinguishable_table_and_bounds)
{
    CHECK_EQ(8, techDistinguishable(0x00));  // Mario
    CHECK_EQ(4, techDistinguishable(0x06));  // Fox
    CHECK_EQ(10, techDistinguishable(0x09)); // Captain Falcon
    CHECK_EQ(3, techDistinguishable(0x0D));  // Zelda
    CHECK_EQ(4, techDistinguishable(0x0E));  // Sheik
    CHECK_EQ(8, techDistinguishable(0x0F));  // Popo
    CHECK_EQ(8, techDistinguishable(0x10));  // Nana
    CHECK_EQ(2, techDistinguishable(0x1E));  // Squirtle
    CHECK_EQ(kTechDistinguishableMissing, techDistinguishable(0x1C)); // Pokemon Trainer
    CHECK_EQ(kTechDistinguishableMissing, techDistinguishable(0x31)); // Wario-Man
    CHECK_EQ(kTechDistinguishableMissing, techDistinguishable(-1));
    CHECK_EQ(kTechDistinguishableMissing, techDistinguishable(kTechDistinguishableCount));
}

TEST(tech_frames, invisibility_and_sound_rules)
{
    // Fox (dist 4), delay 0: visible 1..4, invisible 5..20, visible again at 21.
    for (int f = 1; f <= 25; f++)
    {
        CHECK_EQ(f >= 5 && f <= 20, techInvisFrame(f, 4, 0));
        CHECK_EQ(f == 4, techSoundFrame(f, 4));
    }
    // Fox (dist 4), delay 3: visible 1..7, invisible 8..20, visible again at 21.
    for (int f = 1; f <= 25; f++)
    {
        CHECK_EQ(f >= 8 && f <= 20, techInvisFrame(f, 4, 3));
    }
    // Mario (dist 8), delay 0 and delay 3; delay 15 pushes past frame 21 (never hidden).
    for (int f = 1; f <= 25; f++)
    {
        CHECK_EQ(f >= 9 && f <= 20, techInvisFrame(f, 8, 0));
        CHECK_EQ(f >= 12 && f <= 20, techInvisFrame(f, 8, 3));
        CHECK(!techInvisFrame(f, 8, 15));
        CHECK_EQ(f == 8, techSoundFrame(f, 8));
    }
    // Missing entry (-1): neither invisibility nor Tech Sound applies.
    for (int f = 1; f <= 25; f++)
    {
        CHECK(!techInvisFrame(f, kTechDistinguishableMissing, 0));
        CHECK(!techSoundFrame(f, kTechDistinguishableMissing));
    }
    // ProjectPunch get-ups (0-indexed subactionFrame): hidden for 5 + delay < subactionFrame < 20.
    for (int f = 0; f <= 25; f++)
    {
        CHECK_EQ(f >= 6 && f <= 19, getupInvisFrame(f, 0));
        CHECK_EQ(f >= 9 && f <= 19, getupInvisFrame(f, 3));
        CHECK(!getupInvisFrame(f, 15));
    }
}
