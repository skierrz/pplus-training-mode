// Character RNG (character_rng.h): Peach's forward-smash weapon, her
// down-special pull and the turnip's face, for every Peach in the match,
// Luigi's side-special misfire, for every Luigi, and Game & Watch's Judge
// number, for every Game & Watch.
// Game state: her smash flags; the pull odds in her fighter data and the
// turnip's draw weights in the loaded item file (both put back at detach);
// Luigi's misfire count; G&W's two Judge history ints.
// Read from game memory with a Peach in the match (ft_peach, FitPeach.pac,
// the item file); scripts/rng_tests.py measures what is marked "measured".
// Long form: docs/internals-extra.md, "Character RNG: Peach".
#include "extra/character_rng.h"

#include <ft/ft_common_data_accesser.h>
#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <so/item/so_item_manage_module_impl.h>
#include <so/motion/so_motion_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/so_value_accesser.h>
#include <so/stageobject.h>
#include <so/work/so_work_manage_module_impl.h>
#include <sr/sr_common.h>

#include "extra/fighter_mechanics.h"
#include "extra/fighter_mechanics_logic.h"
#include "extra/fighter_swap.h"
#include "extra/net_alloc.h"
#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/settings.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/log.h"

// P+ 3.2.1, where Peach decides:
//  - Forward smash: P+'s script ignores the module's draw (status 0x2A's
//    start, randi(3) into instance int 64) and cycles its flags 104-106 in
//    the status work (past its 64 flags, bytes the game never clears; one is
//    always set, 106 from the heap's fill pattern): club, pan, racket per
//    Peach (measured). The hitboxes follow the same flags.
//  - Pull (status 0x115): one in N (parameter 24001; rule read in her
//    module, measured at N = 1 and -1 only) draws an item by weight (kind
//    into status int 2, else -1), else a turnip (article 2, item 0x56), made
//    on the animation's first frame in the same game step (measured). P+'s
//    FitPeach.pac: N = 80, Bob-omb 0, Mr. Saturn 3, Beam Sword 0 (read in
//    data; Brawl's file: 2, 3, 1). The choice is written into N (measured).
//  - Turnip face (ItmCommonParam.pac's ItmPeachDaikonParam, read in data):
//    the turnip's start script draws it by weight into instance int 0 (item
//    command 0x6513: the weights' sum times a random 0 to 1, read in code),
//    sets int 1, the thrown hitbox's damage (22 for the stitch face:
//    measured), and stops its "Face" texture animation on frame <face>
//    (ItmCommonBrres.pac's ItmPeachDaikonBrres, texture ItmPeachDkF.<face>;
//    pictures seen by decoding them). Against Brawl, P+ moved one weight from
//    the smile to the dot eyes (Brawl: 35 and 1) and lowered the stitch
//    face's damage (Brawl: 30). Only the chosen face's weight is left
//    (measured: that face from the pull's first frame). The item files stay
//    loaded from boot (put back at detach: measured after leaving Training).
//  - Judge (status 0x113, ft_gamewatch.rel 0x11AE0 in the running module): the
//    number played is LA-Basic[67] (0-8 for 1-9) when the status starts; the
//    start draws the next one avoiding int 67 and int 68 into 67 and copies the
//    active number into int 68, which ft_gamewatch.rel 0x0B9D0 -> 0x1AB3C reads
//    on frame 15 to set wnGameWatchPanel's visible sign digit. Outside 0x113,
//    int 68 is set to choice % 9 so 0x11AE0 never sees 67 == 68; during 0x113,
//    int 68 is left untouched so the visible sign matches the hitbox, while
//    int 67 stays choice - 1 for P+'s 1-dot/2-dot parity indicator.
//  - Misfire (FitLuigi.pac, action 0x118's entry): a misfire unless instance
//    int 91 is below 7 or flag 113 (the misfire stored, set while charging)
//    is on. A side special that does not misfire adds 1 to int 91 while it is
//    below 7; a misfire, and every frame of his Wait subaction while int 53
//    is 0, set it to a random 0 to 7 (and the misfire's damage, int 92, to
//    27). Always and Never move the counter across 7; Default leaves it alone.
namespace tm {
namespace extra {
    enum {
        kStatusSmashStart = 0x2A,
        kStatusSmashHold = 0x2B,
        kStatusSmash = 0x2C,
        kStatusJudge = 0x113,
        kStatusPull = 0x115,
        // The script's weapon flags, as its commands name them.
        kFlagGolfClub = 0x20000068,
        kFlagFryingPan = 0x20000069,
        kFlagTennisRacket = 0x2000006A,
        // Her fighter data: the pointer to the one-in-N parameter, and her
        // item table (entries of {item kind, weight}, count).
        kDataOdds = 0x84,
        kDataItems = 0x94,
        kDataItemCount = 0x98,
        kParamOdds = 24001,
        kItemEntries = 3,
        kOddsNever = -1,
        kOddsAlways = 1,
        // The turnip's instance ints and its face animation.
        kTurnipFace = 0x10000000,
        kTurnipPower = 0x10000001,
        kAnimFace = 7,
        // Its script's weighted draw: the command word (item command
        // 0x6513, 9 arguments), which the file places 0x158 bytes after
        // the arguments, and the arguments' types.
        kCmdDraw = 0x65130900,
        kCmdAfterArgs = 0x158,
        kArgScalar = 1,
        kArgVariable = 5,
        kScalarOne = 60000,
        kFaces = 8,
        // Game & Watch's Judge history: the next number, the one before.
        kJudgeNext = 0x10000043,
        kJudgeBefore = 0x10000044,
        // Luigi's misfire count, and the count from which side special
        // misfires.
        kMisfireCount = 0x1000005B,
        kMisfireAt = 7
    };

    // The draw's weights in P+'s file (scalars, x kScalarOne), /58: Smile,
    // T Eyes, Line Eyes, Circle Eyes, Arch Eyes, Winky, Dot Eyes, Stitch Face.
    static const s32 kFileWeights[kFaces] = {34, 6, 5, 3, 3, 4, 2, 1};

    struct ScriptArg {
        s32 type;
        s32 value;
    };

    struct ItemEntry {
        s32 kind;
        float weight;
    };

    // The parameter in the match's fighter data, once found; tied to the live
    // g_ftCommonDataAccesser data pointer so a swap out of Peach drops it.
    static const void* s_oddsData = NULL;
    static s32* s_odds = NULL;
    static s32 s_oddsFile = 0;
    static bool s_oddsTried = false;
    // Peach Forward Smash as it was last applied.
    static u8 s_smashWas = main::kSmashDefault;
    // The turnip script's draw arguments (the face variable, then the eight
    // weights), once found.
    static ScriptArg* s_draw = NULL;
    static bool s_drawTried = false;

    // Face and thrown damage per main::PeachTurnip, as the turnip's script
    // pairs them.
    static const s8 kFace[main::kPeachTurnipCount] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, -1};
    static const s8 kPower[main::kPeachTurnipCount] = {0, 2, 2, 2, 2, 2, 6, 12, 22, 0};

    static int activeKind(const game::FighterRef& ref)
    {
        ftEntryManager* entries = extraEntryManager();
        if (!game::isRam(entries))
        {
            return -1;
        }
        ftEntry* entry = entries->getEntity(ref.entryId);
        if (!game::isRam(entry) || entry->m_entryId != ref.entryId)
        {
            return -1;
        }
        int active = entry->m_activeInstanceIndex;
        return (active >= 0 && active < 4) ? static_cast<int>(entry->m_instances[active].m_kind) : -1;
    }

    static bool hasLivePartner(const game::FighterRef& ref)
    {
        if (ref.partner == NULL)
        {
            return false;
        }
        const soModuleEnumeration* pm = game::fighterModules(ref.partner);
        if (pm == NULL)
        {
            return false;
        }
        int status = game::fighterStatus(pm);
        return status >= 0 && status != game::kStatusDead && status != game::kStatusStandby;
    }

    static bool matchesQuery(int call, int kind, bool livePartner)
    {
        if (call == main::kRngAny)
        {
            return fighterHasMechanicsRow(kind) || (kind == kFtPopo && livePartner);
        }
        if (call == main::kRngIceClimbers)
        {
            return kind == kFtPopo && livePartner;
        }
        static const u8 kKinds[] = {
            kFtPeach, kFtGameWatch, kFtLuigi, kFtSamus, kFtMewtwo, kFtLucario, kFtWario, kFtDonkeyKong,
            kFtIvysaur, kFtRob, kFtLucas, kFtSquirtle, kFtSheik, kFtDedede, kFtSnake, kFtGanondorf, 0xFF, kFtOlimar
        };
        int idx = call - main::kRngPeach;
        return idx >= 0 && idx < static_cast<int>(sizeof(kKinds)) && kind == static_cast<int>(kKinds[idx]);
    }

    // The next Judge is the chosen one. The one before is set to another
    // number outside status 0x113: with both the same, 0x11AE0 draws twice and
    // shows the draw. During 0x113, ft_gamewatch.rel 0x0B9D0 reads kJudgeBefore
    // (copied from kJudgeNext on entry) for wnGameWatchPanel's visible digit,
    // so leave kJudgeBefore untouched until the Judge ends.
    static void applyJudge(const soModuleEnumeration* m, int status, int choice)
    {
        if (choice == main::kHammerDefault)
        {
            return;
        }
        soWorkManageModule* work = m->m_workManageModule;
        work->setInt(choice - 1, kJudgeNext);
        if (status != kStatusJudge)
        {
            work->setInt(choice % 9, kJudgeBefore);
        }
    }

    // A misfire stored while charging still gives a normal side special
    // under Always: the store's flag is left to the script.
    static void applyMisfire(const soModuleEnumeration* m, int choice)
    {
        soWorkManageModule* work = m->m_workManageModule;
        int count = work->getInt(kMisfireCount);
        if (choice == main::kMisfireAlways && count < kMisfireAt)
        {
            work->setInt(kMisfireAt, kMisfireCount);
        }
        else if (choice == main::kMisfireNever && count >= kMisfireAt)
        {
            work->setInt(kMisfireAt - 1, kMisfireCount);
        }
    }

    static void setFlag(soWorkManageModule* work, u32 flag, bool on)
    {
        // What the script's "bit variable set" and "clear" commands call.
        if (on)
        {
            work->onFlag(flag);
        }
        else
        {
            work->offFlag(flag);
        }
    }

    // `was`: the row's value when it was last applied.
    static void applySmash(const soModuleEnumeration* m, int status, int choice, int was)
    {
        if (choice == was && choice == main::kSmashDefault)
        {
            return;
        }
        // The smash's scripts read the flags: hands off until it has ended.
        if (status == kStatusSmashStart || status == kStatusSmashHold || status == kStatusSmash)
        {
            return;
        }
        // The weapon before the chosen one in the script's order (the
        // script moves on to the chosen one); back on Default, the one that
        // was chosen, as the script left it.
        int shown = choice == main::kSmashDefault ? was : (choice + 1) % 3 + 1;
        soWorkManageModule* work = m->m_workManageModule;
        setFlag(work, kFlagGolfClub, shown == main::kSmashGolfClub);
        setFlag(work, kFlagFryingPan, shown == main::kSmashFryingPan);
        setFlag(work, kFlagTennisRacket, shown == main::kSmashTennisRacket);
    }

    static const soModuleEnumeration* objectModules(StageObject* object)
    {
        if (!game::isRam(object) || !game::isRam(object->m_moduleAccesser))
        {
            return NULL;
        }
        const soModuleEnumeration* m = object->m_moduleAccesser->m_enumerationStart;
        return game::isRam(m) ? m : NULL;
    }

    static void applyFace(const soModuleEnumeration* m, int status, int choice)
    {
        int face = kFace[choice];
        if (face < 0 || status != kStatusPull)
        {
            return;
        }
        soItemManageModule* items = m->m_itemManageModule;
        if (!game::isRam(items) || !items->isHaveItem(0) || items->getHaveItemKind(0) != Item_Peach_Daikon)
        {
            return;
        }
        soItemInfo info;
        items->getHaveItemInfo(&info, 0);
        const soModuleEnumeration* turnip = objectModules(reinterpret_cast<StageObject*>(info.m_item));
        if (turnip == NULL || !game::isRam(turnip->m_workManageModule) || !game::isRam(turnip->m_motionModule))
        {
            return;
        }
        soWorkManageModule* work = turnip->m_workManageModule;
        soMotionModule* motion = turnip->m_motionModule;
        // Backstop for the weights (face 0 for a random 0 exactly; weights
        // not found): once the script has drawn (damage one of its four,
        // face animation there), face, damage and frame are set here.
        int power = work->getInt(kTurnipPower);
        if ((power != 2 && power != 6 && power != 12 && power != 22) || motion->getOtherAnimSize() < 1
            || motion->getOtherAnimKind(0) != kAnimFace || work->getInt(kTurnipFace) == face)
        {
            return;
        }
        work->setInt(face, kTurnipFace);
        work->setInt(kPower[choice], kTurnipPower);
        motion->setOtherAnimFrame(static_cast<float>(face), 0);
    }

    // Whether `args` is the turnip's draw as Project+'s file has it: the face
    // variable, then the eight weights as scalars. With `anyWeights`, the
    // weights may be any scalars (this module may have changed them).
    static bool isDraw(const ScriptArg* args, bool anyWeights)
    {
        if (!game::isRam(args) || args[0].type != kArgVariable || args[0].value != kTurnipFace)
        {
            return false;
        }
        for (int i = 0; i < kFaces; i++)
        {
            if (args[1 + i].type != kArgScalar || (!anyWeights && args[1 + i].value != kFileWeights[i] * kScalarOne))
            {
                return false;
            }
        }
        return true;
    }

    // The turnip's draw in the item resource heap (ItmCommonParam.pac),
    // searched from the heap's end, where that file lies (measured), once per
    // match. Not via the command's argument pointer: at the match's start it
    // still holds the file's offset, 0xA8 (measured).
    static void findDraw()
    {
        s_drawTried = true;
        game::HeapUsage heap;
        if (!game::heapUsage(Heaps::ItemResource, heap))
        {
            TM_LOG("character rng: no item resource heap: the turnip's face is put in after its draw\n");
            return;
        }
        const u32* first = reinterpret_cast<const u32*>(heap.start);
        for (const u32* w = reinterpret_cast<const u32*>(heap.end) - kCmdAfterArgs / 4 - 1; w >= first; w--)
        {
            if (w[0] == kArgVariable && w[1] == kTurnipFace && w[kCmdAfterArgs / 4] == kCmdDraw
                && isDraw(reinterpret_cast<const ScriptArg*>(w), false))
            {
                s_draw = reinterpret_cast<ScriptArg*>(const_cast<u32*>(w));
                break;
            }
        }
        TM_LOG("character rng: turnip face draw at %08x%s\n", s_draw,
               s_draw != NULL ? "" : " not found: the turnip's face is put in after its draw");
    }

    // The file's weights for Default and Mr. Saturn, else only the chosen
    // face's.
    static void applyDraw(int choice)
    {
        if (!s_drawTried)
        {
            findDraw();
        }
        if (s_draw == NULL || !isDraw(s_draw, true))
        {
            return;
        }
        int face = kFace[choice];
        for (int i = 0; i < kFaces; i++)
        {
            s32 want = face < 0 ? kFileWeights[i] * kScalarOne : (i == face ? kScalarOne : 0);
            if (s_draw[1 + i].value != want)
            {
                s_draw[1 + i].value = want;
            }
        }
    }

    static void clearOddsCache()
    {
        s_oddsData = NULL;
        s_odds = NULL;
        s_oddsFile = 0;
        s_oddsTried = false;
    }

    // True only while Peach's same loaded fighter data is still registered in
    // g_ftCommonDataAccesser (ftDataProvider::remove zeroes the slot at
    // 0x80850334 when freeing FitPeach.pac (in the same synchronous call))
    // and still points at s_odds.
    static bool oddsResident()
    {
        if (s_odds == NULL || s_oddsData == NULL)
        {
            return false;
        }
        const void* data = extraFtCommonData(Fighter_Peach);
        return data == s_oddsData && game::isRam(data)
               && reinterpret_cast<s32*>(game::wordAt(data, kDataOdds)) == s_odds && game::isRam(s_odds);
    }

    static void syncOddsCache()
    {
        const void* data = extraFtCommonData(Fighter_Peach);
        if (data == NULL || data != s_oddsData)
        {
            clearOddsCache();
        }
    }

    // Finds the one-in-N parameter in Peach's fighter data; the item part
    // stays off unless it is what the game reads as parameter 24001 and her
    // item table is the one this was built for.
    static void findOdds(Fighter* fighter)
    {
        s_oddsTried = true;
        const void* data = extraFtCommonData(Fighter_Peach);
        if (!game::isRam(data))
        {
            s_oddsData = NULL;
            TM_LOG("character rng: Peach's fighter data not found: item pulls left alone\n");
            return;
        }
        s_oddsData = data;
        s32* odds = reinterpret_cast<s32*>(game::wordAt(data, kDataOdds));
        const ItemEntry* items = reinterpret_cast<const ItemEntry*>(game::wordAt(data, kDataItems));
        u32 count = game::wordAt(data, kDataItemCount);
        bool ok = game::isRam(odds) && game::isRam(items) && count == kItemEntries
                  && extraConstInt(fighter->m_moduleAccesser, kParamOdds) == *odds && *odds > 1
                  && items[0].kind == Item_BobOmb && items[0].weight == 0.0f && items[1].kind == Item_MrSaturn
                  && items[1].weight > 0.0f && items[2].kind == Item_BeamSword && items[2].weight == 0.0f;
        TM_LOG("character rng: Peach's item odds at %08x (1 in %d), table at %08x (%d): %s\n", odds,
               game::isRam(odds) ? *odds : 0, items, count, ok ? "ok" : "not as expected: item pulls left alone");
        if (ok)
        {
            s_odds = odds;
            s_oddsFile = *odds;
        }
    }

    static void applyOdds(Fighter* fighter, int choice)
    {
        syncOddsCache();
        if (!s_oddsTried)
        {
            findOdds(fighter);
        }
        if (!oddsResident())
        {
            return;
        }
        s32 want = choice == main::kTurnipDefault ? s_oddsFile
                                                  : (choice == main::kTurnipMrSaturn ? kOddsAlways : kOddsNever);
        if (*s_odds != want)
        {
            *s_odds = want;
        }
    }

    void characterRngAttach()
    {
        clearOddsCache();
        s_smashWas = main::kSmashDefault;
        s_draw = NULL;
        s_drawTried = false;
        fighterMechanicsAttach();
    }

    static void restoreOdds(bool preSwap)
    {
        if (oddsResident())
        {
            const void* liveData = extraFtCommonData(Fighter_Peach);
            TM_LOG("character rng: %s item odds %08x (was %d, put back %d, live data %08x)\n",
                   preSwap ? "pre-swap restore" : "detach", s_odds, *s_odds, s_oddsFile, liveData);
            *s_odds = s_oddsFile;
        }
        clearOddsCache();
    }

    void characterRngPrepareSwap()
    {
        fighterMechanicsPrepareSwap();
        restoreOdds(true);
        s_smashWas = main::kSmashDefault;
    }

    void characterRngDetach()
    {
        fighterMechanicsDetach();
        restoreOdds(false);
        // The item files stay loaded for every later match and mode: the
        // file's weights must be back.
        if (s_draw != NULL && isDraw(s_draw, true))
        {
            for (int i = 0; i < kFaces; i++)
            {
                s_draw[1 + i].value = kFileWeights[i] * kScalarOne;
            }
            TM_LOG("character rng: turnip face draw at %08x put back\n", s_draw);
        }
        s_draw = NULL;
    }

    bool characterRng(int call)
    {
        if (call == main::kRngApply && fighterSwapBusy())
        {
            return false;
        }
        if (call == main::kRngApply)
        {
            syncOddsCache();
            fighterMechanicsSync();
        }
        const main::CharacterRngSettings& s = main::settings().characterRng;
        int turnip = s.peachTurnip < main::kPeachTurnipCount ? s.peachTurnip : main::kTurnipDefault;
        int smash = s.peachSmash < main::kPeachSmashCount ? s.peachSmash : main::kSmashDefault;
        int judge = s.gnwHammer < main::kGnwHammerCount ? s.gnwHammer : main::kHammerDefault;
        int misfire = s.luigiMisfire < main::kLuigiMisfireCount ? s.luigiMisfire : main::kMisfireDefault;
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        bool matched = false;
        bool peach = false;
        for (int i = 0; i < n; i++)
        {
            int kind = activeKind(refs[i]);
            if (kind < 0)
            {
                continue;
            }
            if (call != main::kRngApply && matchesQuery(call, kind, hasLivePartner(refs[i])))
            {
                matched = true;
            }
            if (kind == kFtPeach)
            {
                peach = true;
            }
            const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
            if (call != main::kRngApply || m == NULL || !game::isRam(m->m_workManageModule))
            {
                continue;
            }
            int status = game::fighterStatus(m);
            if (kind == kFtGameWatch)
            {
                applyJudge(m, status, judge);
            }
            else if (kind == kFtLuigi && misfire != main::kMisfireDefault)
            {
                applyMisfire(m, misfire);
            }
            else if (kind == kFtPeach)
            {
                applySmash(m, status, smash, s_smashWas);
                applyOdds(refs[i].fighter, turnip);
                applyDraw(turnip);
                applyFace(m, status, turnip);
            }
            fighterMechanicsApplyFighter(refs[i].fighter, m, kind, status, refs[i].playerNo);
        }
        if (call == main::kRngApply)
        {
            s_smashWas = static_cast<u8>(smash);
            return peach;
        }
        return matched;
    }
}
}
