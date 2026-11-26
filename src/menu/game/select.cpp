// Training's character select shaped by the chosen drill's rule; see
// select.h. Changes the selection data (GameGlobal+0x10) and, while the
// select runs, places in its code (sora_menu_sel_char, RSBE01 rev 2 with
// P+ 3.2.1) and data: the player-area count; the coin's pickup, recall, drop
// and hover; the icon list and its layout counts. Darkens icons (their
// models' light channels) while a limited coin is held.
#include "menu/game/select.h"

#include <OS/OSCache.h>
#include <gf/gf_scene.h>
#include <gm/gm_global.h>

#include "menu/game/training.h"
#include "shared/game/mem.h"
#include "shared/game/menu_sound.h"
#include "shared/log.h"
#include "shared/select_rules.h"

// Bit n set: the coin of player area n can't be picked up (read by lockHook).
extern "C" {
u32 tmSelectLockMask = 0;
}

namespace tm {
namespace menu {
namespace select {
    using game::isRam;
    using drills::SelectRule;

    // The selection data, as training.cpp writes it for a direct start.
    // Player 0 is the human (area 0), player 1 the CPU (area 1, which the
    // select makes the CPU in Training: 0x80685ADC-EC).
    enum {
        kSelHasChoice = 0x7,
        kSelIcons = 0x8, // a word per player, the icon in bits 8-15
        kSelPlayers = 0xB8,
        kPlayerSize = 0x5C,
        kPlayerKind = 0x0,
        kPlayerState = 0x1, // 0 human, 1 CPU, 3 none
        kPlayerStocks = 0x4,
        kPlayerColorFile = 0x5,
        kPlayerColor = 0x6,
        kPlayerController = 0x7,
        kStateCpu = 1,
        kStateNone = 3,
        kAreaHuman = 0,
        kAreaCpu = 1
    };

    struct Patch {
        const char* what;
        u32 at;
        u32 game;    // the game's word there
        u32 context; // a word next to it that must be the game's too
        u32 contextAt;
        bool on;
        u32 ours;
    };

    // The player-area count, set in the task's init (0x8068342C-DC): Training
    // (mode 10) takes the `li r0,2` at 0x806834D0 before `stw r0,0x648(r30)`.
    // Only Training's select loads this module, so the word is changed, not
    // the mode test.
    static Patch s_oneArea = {"area count", 0x806834D0, 0x38000002, 0x901E0648, 0x806834DC, false, 0x38000001};

    // The coin pickup on A (0x8068ADC4): 0x8069DB68 returns 1 with the area
    // of the coin under the hand at r1+8, then `cmpwi r3,1` / `bne-` skips
    // the pickup. The compare becomes a call to lockHook. LR is free there:
    // the function saved it in its prologue and reloads it before returning.
    static Patch s_lock = {"coin pickup", 0x8068AE7C, 0x2C030001, 0x408200C8, 0x8068AE80, false, 0};

    // As the select would: r3 == 1 with the coin's area index at the
    // caller's r1+8; a locked area's coin answers 0 (no pickup). Leaves the
    // compare in cr0 for the bne that follows.
    static asm void lockHook()
    {
        nofralloc
        cmpwi r3, 1
        bnelr
        lwz r0, 8(r1)
        lis r12, tmSelectLockMask@h
        ori r12, r12, tmSelectLockMask@l
        lwz r12, 0(r12)
        srw r12, r12, r0
        clrlwi. r12, r12, 31
        beq unlocked
        li r3, 0
    unlocked:
        cmpwi r3, 1
        blr
    }

    // B calls an area's coin back to its own hand (0x80689DD4-E64, with the
    // area index in r25): it goes ahead when the hand holds no coin, `lwz
    // r0,0xA0(r30)` / `cmpwi r0,0` / `bne-` past it. The compare becomes a
    // call to recallHook. LR is free there as above (0x80689EB4 reloads it).
    static Patch s_recall = {"coin recall", 0x80689DD8, 0x2C000000, 0x801E00A0, 0x80689DD4, false, 0};

    // A locked area's hand answers "holding a coin" (no recall).
    static asm void recallHook()
    {
        nofralloc
        lis r12, tmSelectLockMask@h
        ori r12, r12, tmSelectLockMask@l
        lwz r12, 0(r12)
        srw r12, r12, r25
        clrlwi. r12, r12, 31
        beq unlocked
        li r0, 1
    unlocked:
        cmpwi r0, 0
        blr
    }

    // Dropping the held coin (A, 0x8068ADF8-E68): the icon under it is read
    // into r27 from its area (r28) +0x1B8, then `cmpwi r27,40` (40: no icon,
    // which P+'s hook at 0x8068AE24 turns into a random pick). The compare
    // becomes a call to dropHook; a refused drop leaves through the
    // function's own epilogue (0x8068AF48), where the coin stays in hand.
    static Patch s_drop = {"coin drop", 0x8068AE1C, 0x2C1B0028, 0x837C01B8, 0x8068AE18, false, 0};
    static const u32 kDropEpilogueWord = 0x39610040; // addi r11,r1,64

    extern "C" {
    u32 tmSelectDropExit = 0x8068AF48;
    void tmSelectDropRefused();
    }

    // Icons area n's coin may take: bit i of word 2n + i / 32 (all set: any).
    // Area index: muSelCharPlayerArea +0x1B0 (BrawlHeaders m_areaIdx; the
    // select passes it from there at 0x8068AE58).
    extern "C" {
    u32 tmSelectAllowed[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    }

    static asm void dropHook()
    {
        nofralloc
        lwz r12, 0x1B0(r28)
        cmplwi r12, 1
        bgt allowed
        cmplwi r27, 63
        bgt allowed
        slwi r12, r12, 3
        srwi r11, r27, 5
        slwi r11, r11, 2
        add r12, r12, r11
        lis r11, tmSelectAllowed@h
        ori r11, r11, tmSelectAllowed@l
        lwzx r11, r11, r12
        clrlwi r12, r27, 27
        srw r11, r11, r12
        clrlwi. r11, r11, 31
        beq refused
    allowed:
        cmpwi r27, 40
        blr
    refused:
        lis r12, tmSelectDropExit@h
        ori r12, r12, tmSelectDropExit@l
        lwz r12, 0(r12)
        mtlr r12
        lis r12, tmSelectDropRefused@h
        ori r12, r12, tmSelectDropRefused@l
        mtctr r12
        bctr
    }

    // ---- a refused icon is dead to the held coin ------------------------
    //
    // moveCoin (0x80689548, per area, P+ Teams.asm:116 names it) puts the
    // icon under the coin in r31 (the lookup 0x806913B8, its only caller;
    // 40 off the grid, 0x806895E4); where the two paths meet (0x806895E8,
    // `lwz r0,0x1B8(r27)`) it compares that with the area's kind and, when
    // they differ, sets the kind (the panel's portrait), plays the icon's
    // hover animation (0x8068FE04, 3) and the hover sound (playSE 0 at
    // 0x80689784). That load becomes a call to hoverHook: an icon the coin's
    // area can't take reads as 40, empty grid space, so none of that happens
    // and the drop (which reads the kind) is refused.
    static Patch s_hover = {"coin hover", 0x806895E8, 0x801B01B8, 0x3BE00028, 0x806895E4, false, 0};

    static asm void hoverHook()
    {
        nofralloc
        cmplwi r30, 1
        bgt done
        cmplwi r31, 63
        bgt done
        slwi r12, r30, 3
        srwi r11, r31, 5
        slwi r11, r11, 2
        add r12, r12, r11
        lis r11, tmSelectAllowed@h
        ori r11, r11, tmSelectAllowed@l
        lwzx r11, r11, r12
        clrlwi r12, r31, 27
        srw r11, r11, r12
        clrlwi. r11, r11, 31
        bne done
        li r31, 40
    done:
        lwz r0, 0x1B8(r27)
        blr
    }

    // ---- the grid without the icons no coin can take --------------------
    //
    // The select's init copies P+'s roster (CSS.ASM:47-58, from 0x80680DE0,
    // which the code list writes again every frame) into the task's icon
    // list (+0x70, a word per icon) and stores the count at 0x8068317C. The
    // `mr r3,r30` the copy branches to (0x80683170) becomes a call to
    // rosterHook, which drops the icons outside tmSelectGrid, keeping the
    // order; the icons fill the first slots of P+'s layout.
    static Patch s_roster = {"roster", 0x80683170, 0x7FC3F378, 0x937E0138, 0x8068317C, false, 0};

    // The layout (P+ has only its ninth, FaceI) is the first of nine counts
    // at 0x806A0AFC (10, 12, 18, 21, 24, 32, 36, 40, 50) the icon count
    // doesn't exceed; three places read them (0x8068E6F8, 0x8068EC4C,
    // 0x80690010). The first eight become 0, so any count up to 50 gets P+'s.
    enum {
        kLayoutCounts = 0x806A0AFC,
        kLayouts = 9
    };
    static const u32 kLayoutCount[kLayouts] = {10, 12, 18, 21, 24, 32, 36, 40, 50};
    static bool s_layoutOn = false;

    // Two paths turn a coin with no character (40) into Random (41) and look
    // its icon up, which a grid without Random doesn't have (the lookup at
    // 0x80690318 answers -1 and the coin is put at an unset position): the
    // panel's player-kind button (0x8068B808) and a hand's controller
    // leaving (0x806892B4). Their Random branch is skipped instead.
    static Patch s_kindRandom = {"kind button random", 0x8068B814, 0x7FC3F378, 0x4082021C, 0x8068B810, false,
                                 0x48000218}; // b 0x8068BA2C
    static Patch s_padRandom = {"pad leaving random", 0x806892BC, 0x7F83E378, 0x40820024, 0x806892B8, false,
                                0x48000030}; // b 0x806892EC

    extern "C" {
    u32 tmSelectGrid[2] = {0xFFFFFFFF, 0xFFFFFFFF}; // icons the grid keeps: bit i of word i / 32
    u32 tmSelectFilterRoster(u8* task, u32 count);
    }

    enum {
        kTaskRoster = 0x70,
        kRosterMax = 50
    };

    static bool s_dark[kRosterMax]; // icon slot n is darkened

    u32 tmSelectFilterRoster(u8* task, u32 count)
    {
        u32* icons = reinterpret_cast<u32*>(task + kTaskRoster);
        u32 kept = 0;
        for (u32 i = 0; i < count && i < kRosterMax; i++)
        {
            u32 icon = icons[i];
            if (icon < 64 && (tmSelectGrid[icon / 32] >> (icon % 32)) & 1)
            {
                icons[kept++] = icon;
            }
        }
        if (kept == 0)
        {
            TM_LOG("select: no roster icon in the drill's sets: grid left whole\n");
            return count;
        }
        TM_LOG("select: grid of %d icons (of %d)\n", kept, count);
        return kept;
    }

    static asm void rosterHook()
    {
        nofralloc
        mflr r0
        stwu r1, -16(r1)
        stw r0, 20(r1)
        mr r3, r30
        mr r4, r27
        lis r12, tmSelectFilterRoster@h
        ori r12, r12, tmSelectFilterRoster@l
        mtctr r12
        bctrl
        mr r27, r3
        lwz r0, 20(r1)
        mtlr r0
        addi r1, r1, 16
        mr r3, r30
        blr
    }

    static const SelectRule* s_built = NULL; // the rule the select was built with
    static bool s_reopening = false;

    static void writeCode(u32 at, u32 word)
    {
        *reinterpret_cast<u32*>(at) = word;
        DCFlushRange(reinterpret_cast<void*>(at), 4);
        ICInvalidateRange(reinterpret_cast<void*>(at), 4);
    }

    static u32 codeAt(u32 at)
    {
        return *reinterpret_cast<const u32*>(at);
    }

    static bool put(Patch& p)
    {
        if (codeAt(p.at) != p.game || codeAt(p.contextAt) != p.context)
        {
            TM_LOG("select: %s at %08x holds %08x %08x, not the game's %08x %08x: left alone\n", p.what, p.at,
                   codeAt(p.at), codeAt(p.contextAt), p.game, p.context);
            return false;
        }
        writeCode(p.at, p.ours);
        p.on = true;
        return true;
    }

    static void undo(Patch& p)
    {
        if (p.on && codeAt(p.at) == p.ours)
        {
            writeCode(p.at, p.game);
        }
        p.on = false;
    }

    static u8* selData()
    {
        GameGlobal* g = g_GameGlobal;
        return isRam(g) && isRam(g->m_selCharData) ? reinterpret_cast<u8*>(g->m_selCharData) : NULL;
    }

    static u8* player(u8* sel, int i)
    {
        return sel + kSelPlayers + i * kPlayerSize;
    }

    static int iconOf(u8* sel, int i)
    {
        return sel[kSelIcons + 4 * i + 2];
    }

    // Puts player `i`'s coin on `icon` when the select is built. Changing the
    // icon resets its costume: initPlayerArea (0x80685BE8) restores +5 with no
    // bound check against the character's costume count (muMenu::getNumCharColor,
    // 0x800AF8D0), so a high alt from a character with more costumes leaves a
    // portrait blank. Keeping the same icon preserves the chosen costume.
    static void preset(u8* sel, int i, int icon)
    {
        if (!sel[kSelHasChoice] || iconOf(sel, i) != icon)
        {
            player(sel, i)[kPlayerColorFile] = 0;
            player(sel, i)[kPlayerColor] = 0;
        }
        sel[kSelIcons + 4 * i + 2] = static_cast<u8>(icon);
        int kind = drills::kindOfIcon(icon);
        if (kind >= 0)
        {
            player(sel, i)[kPlayerKind] = static_cast<u8>(kind);
        }
        if (i == kAreaCpu)
        {
            player(sel, i)[kPlayerState] = kStateCpu;
        }
        sel[kSelHasChoice] = 1;
    }

    // One side's set: its only icon is preset and its coin locked; with
    // several, the first is preset if the last choice is not one of them.
    // Either way the coin can only be dropped on the set's icons (a locked
    // coin too, should it reach a hand by a path the lock does not cover,
    // such as a controller connecting: 0x80689360).
    static void applySet(u8* sel, int i, const u8* set)
    {
        if (drills::anyIcon(set))
        {
            return;
        }
        int only = drills::onlyIcon(set);
        if (only != drills::kIconEnd)
        {
            preset(sel, i, only);
            tmSelectLockMask |= 1u << i;
        }
        else if (!sel[kSelHasChoice] || !drills::setHas(set, iconOf(sel, i)))
        {
            preset(sel, i, set[0]);
        }
        tmSelectAllowed[2 * i] = tmSelectAllowed[2 * i + 1] = 0;
        for (int k = 0; k < drills::kSetSize && set[k] != drills::kIconEnd; k++)
        {
            tmSelectAllowed[2 * i + set[k] / 32] |= 1u << (set[k] % 32);
        }
    }

    static bool restricted()
    {
        for (int i = 0; i < 4; i++)
        {
            if (tmSelectAllowed[i] != 0xFFFFFFFF)
            {
                return true;
            }
        }
        return false;
    }

    static void allowAll()
    {
        for (int i = 0; i < 4; i++)
        {
            tmSelectAllowed[i] = 0xFFFFFFFF;
        }
    }

    // A bl from the patch's site to `hook`, within the branch's 32 MB reach.
    static bool putCall(Patch& p, void (*hook)())
    {
        u32 target = reinterpret_cast<u32>(hook);
        s32 offset = static_cast<s32>(target - p.at);
        if (offset < -0x2000000 || offset >= 0x2000000)
        {
            TM_LOG("select: %s hook at %08x is out of reach of %08x\n", p.what, target, p.at);
            return false;
        }
        p.ours = 0x48000001u | (static_cast<u32>(offset) & 0x03FFFFFCu);
        return put(p);
    }

    // Both or neither.
    static bool lockHooksInstalled()
    {
        if (putCall(s_lock, lockHook) && putCall(s_recall, recallHook))
        {
            return true;
        }
        undo(s_lock);
        return false;
    }

    static bool dropHookInstalled()
    {
        if (codeAt(tmSelectDropExit) != kDropEpilogueWord)
        {
            TM_LOG("select: coin drop's exit at %08x holds %08x: left alone\n", tmSelectDropExit,
                   codeAt(tmSelectDropExit));
            return false;
        }
        return putCall(s_drop, dropHook);
    }

    static bool layoutCountsFound()
    {
        for (int i = 0; i < kLayouts; i++)
        {
            if (codeAt(kLayoutCounts + 4 * i) != kLayoutCount[i])
            {
                TM_LOG("select: layout count %d at %08x holds %08x: grid left whole\n", i, kLayoutCounts + 4 * i,
                       codeAt(kLayoutCounts + 4 * i));
                return false;
            }
        }
        return true;
    }

    static void setLayoutCounts(bool ours)
    {
        for (int i = 0; i < kLayouts - 1; i++)
        {
            writeCode(kLayoutCounts + 4 * i, ours ? 0 : kLayoutCount[i]);
        }
        s_layoutOn = ours;
    }

    static void undoGrid()
    {
        undo(s_roster);
        undo(s_kindRandom);
        undo(s_padRandom);
        if (s_layoutOn && codeAt(kLayoutCounts) == 0)
        {
            setLayoutCounts(false);
        }
        s_layoutOn = false;
        tmSelectGrid[0] = tmSelectGrid[1] = 0xFFFFFFFF;
    }

    // The grid keeps the icons some coin can take: the player's set without
    // a CPU, else the two sets together; a side that takes anyone keeps the
    // whole grid. Random is in no set, so it goes too.
    static void reduceGrid(const SelectRule* r)
    {
        if (!s_drop.on || drills::anyIcon(r->player) || (!r->solo && drills::anyIcon(r->cpu)))
        {
            return;
        }
        tmSelectGrid[0] = tmSelectAllowed[0];
        tmSelectGrid[1] = tmSelectAllowed[1];
        if (!r->solo)
        {
            tmSelectGrid[0] |= tmSelectAllowed[2];
            tmSelectGrid[1] |= tmSelectAllowed[3];
        }
        if (!layoutCountsFound() || !put(s_kindRandom) || !put(s_padRandom) || !putCall(s_roster, rosterHook))
        {
            undoGrid();
            return;
        }
        setLayoutCounts(true);
    }

    void tmSelectDropRefused()
    {
        TM_LOG("select: coin drop refused (not one of the drill's characters): sound %d\n",
               game::playMenuSound(game::kSoundError));
    }

    void attach(int drill)
    {
        s_reopening = false;
        tmSelectLockMask = 0;
        allowAll();
        for (u32 i = 0; i < kRosterMax; i++)
        {
            s_dark[i] = false;
        }
        s_built = drills::selectRuleOf(drill);
        const SelectRule* r = s_built;
        u8* sel = selData();
        if (r == NULL || sel == NULL)
        {
            return;
        }
        applySet(sel, kAreaHuman, r->player);
        if (r->solo)
        {
            put(s_oneArea);
        }
        else
        {
            applySet(sel, kAreaCpu, r->cpu);
        }
        if (tmSelectLockMask != 0 && !lockHooksInstalled())
        {
            tmSelectLockMask = 0;
        }
        if (restricted() && !dropHookInstalled())
        {
            allowAll();
        }
        if (restricted() && !putCall(s_hover, hoverHook))
        {
            // without it a refused icon would still react to the coin
            undo(s_drop);
            allowAll();
        }
        reduceGrid(r);
        TM_LOG("select: built for drill %d: %d area(s)%s, P1 icon %d, CPU icon %d, locked %x, allowed %08x%08x "
               "%08x%08x, grid %08x%08x%s\n",
               drill, s_oneArea.on ? 1 : 2, r->solo && !s_oneArea.on ? " (count not changed)" : "", iconOf(sel, 0),
               iconOf(sel, 1), tmSelectLockMask, tmSelectAllowed[1], tmSelectAllowed[0], tmSelectAllowed[3],
               tmSelectAllowed[2], tmSelectGrid[1], tmSelectGrid[0], s_roster.on ? "" : " (whole)");
    }

    // Solo character-select rules still spawn a second fighter slot in Training
    // (with one area the select writes player 1 as none at 0x80684BBC-C1C), so
    // fill player 1 with P1's fighter at costume 0 as a CPU without overwriting
    // the remembered CPU CSS icon.
    static void addCpu(u8* sel)
    {
        u8* hmn = player(sel, kAreaHuman);
        u8* cpu = player(sel, kAreaCpu);
        if (cpu[kPlayerState] != kStateNone)
        {
            return;
        }
        cpu[kPlayerKind] = hmn[kPlayerKind];
        cpu[kPlayerState] = kStateCpu;
        cpu[kPlayerStocks] = hmn[kPlayerStocks];
        cpu[kPlayerColorFile] = 0;
        cpu[kPlayerColor] = 0;
        cpu[kPlayerController] = 0;
        TM_LOG("select: CPU added for the match: kind %d (P1's)\n", cpu[kPlayerKind]);
    }

    // ---- the icons the held coin can't take, darkened -------------------
    //
    // Each icon is its own model (task +0x16C + 4n, a MuObject with its
    // ScnMdl at +0xC) with per-model copies of its materials' light channels
    // (MuObject asks for them at 0x800B26DC; ScnMdl +0x158, 0x28 bytes a
    // material). Its three materials draw texture times the channel colour,
    // which comes from the vertices (channel control 0x701). With lighting
    // on and no lights the colour is vertex times ambient, so a dark ambient
    // darkens the whole icon, drawn by the game under the hand and coin.
    // The icon's colour animation (MenSelchrFaceI_TopN__0, K0 only) marks
    // the channel for a reset from the model file at the next update (dirty
    // bit 8 at ScnMdl +0x140, 0x801B18FC-0x801B1EC4): a darkened icon's bit
    // is cleared every frame.
    enum {
        kSceneTask = 0x400,
        kTaskAreas = 0x44,
        kTaskRosterCount = 0x138,
        kTaskIcons = 0x16C,
        kTaskAreaCount = 0x648,
        kAreaHand = 0x1A8,
        kAreaCoin = 0x1AC,
        kAreaIndex = 0x1B0,
        kAreaPlayerKind = 0x1B4,
        kAreaControllerNo = 0x1DC,
        kHandCoin = 0xA0,
        kHandMode = 0xA4,
        kHandBButtonCnt = 0xC8,
        kCoinHand = 0x10,
        kCoinArea = 0x14, // BrawlHeaders mu_selchar_coin.h
        kObjectModel = 0xC,
        kModelDirty = 0x140,
        kChanAmbient = 0x8, // channel 0: flag, material, ambient, control, alpha control
        kChanControl = 0xC,
        kLightingOn = 0x2,
        kDirtyChan = 0x8,
        kMaterials = 4
    };
    static const u32 kDarkAmbient = 0x4C4C4C00; // alpha kept

    // nw4r::g3d::ScnMdl::CopiedMatAccess: its constructor, and the copied
    // light channel's getter (0x801AFFDC: field +0x14, dirty bit 8).
    struct MatAccess {
        u32 words[13];
    };
    typedef MatAccess* (*MatAccessCtor)(MatAccess*, void* model, u32 material);
    typedef u8* (*ChanGetter)(MatAccess*, bool markDirty);
    static const u32 kMatAccessCtor = 0x801B0520;
    static const u32 kChanGetter = 0x801AFFDC;
    static const u32 kMatAccessWords[2] = {0x9421FFE0, 0x7C0802A6};
    static const u32 kChanGetterWords[2] = {0x80A30000, 0x2C050000};

    struct Saved {
        u32 control;
        u32 ambient;
    };
    static Saved s_saved[kRosterMax][kMaterials];

    static bool tintCallsFound()
    {
        return codeAt(kMatAccessCtor) == kMatAccessWords[0] && codeAt(kMatAccessCtor + 4) == kMatAccessWords[1]
            && codeAt(kChanGetter) == kChanGetterWords[0] && codeAt(kChanGetter + 4) == kChanGetterWords[1];
    }

    // Darkens or restores the model of icon slot `slot`.
    static void tint(u8* task, u32 slot, bool dark)
    {
        void* object = reinterpret_cast<void*>(game::wordAt(task, kTaskIcons + 4 * slot));
        void* model = isRam(object) ? reinterpret_cast<void*>(game::wordAt(object, kObjectModel)) : NULL;
        if (!isRam(model))
        {
            return;
        }
        u32* dirty = reinterpret_cast<u32*>(game::wordAt(model, kModelDirty));
        for (u32 m = 0; m < kMaterials; m++)
        {
            MatAccess access;
            reinterpret_cast<MatAccessCtor>(kMatAccessCtor)(&access, model, m);
            u8* chan = reinterpret_cast<ChanGetter>(kChanGetter)(&access, false);
            if (!isRam(chan))
            {
                break; // no such material, or no copy of it
            }
            u32* control = reinterpret_cast<u32*>(chan + kChanControl);
            u32* ambient = reinterpret_cast<u32*>(chan + kChanAmbient);
            Saved& saved = s_saved[slot][m];
            if (dark)
            {
                if (!s_dark[slot])
                {
                    saved.control = *control;
                    saved.ambient = *ambient;
                }
                *control = saved.control | kLightingOn;
                *ambient = kDarkAmbient | (saved.ambient & 0xFF);
                if (isRam(dirty))
                {
                    dirty[m] &= ~kDirtyChan;
                }
            }
            else if (s_dark[slot])
            {
                *control = saved.control;
                *ambient = saved.ambient;
            }
        }
        s_dark[slot] = dark;
    }

    static bool allowed(int area, int icon)
    {
        if (area < 0 || area > 1 || icon < 0 || icon > 63)
        {
            return true;
        }
        return (tmSelectAllowed[2 * area + icon / 32] >> (icon % 32)) & 1;
    }

    static u8* areaAt(u8* task, int i)
    {
        u8* area = reinterpret_cast<u8*>(game::wordAt(task, kTaskAreas + 4 * i));
        return isRam(area) ? area : NULL;
    }

    void update()
    {
        if (!s_hover.on || !tintCallsFound())
        {
            return;
        }
        gfSceneManager* m = gfSceneManager::getInstance();
        if (!isRam(m) || !isRam(m->m_currentScene) || !training::selectRunning())
        {
            return;
        }
        u8* task = reinterpret_cast<u8*>(game::wordAt(m->m_currentScene, kSceneTask));
        if (!isRam(task))
        {
            return;
        }
        // the held coin's area; none held: nothing dark
        int idx = -1;
        u8* area = areaAt(task, 0);
        u8* hand = area != NULL ? reinterpret_cast<u8*>(game::wordAt(area, kAreaHand)) : NULL;
        u8* coin = isRam(hand) ? reinterpret_cast<u8*>(game::wordAt(hand, kHandCoin)) : NULL;
        u8* owner = isRam(coin) ? reinterpret_cast<u8*>(game::wordAt(coin, kCoinArea)) : NULL;
        if (isRam(owner))
        {
            idx = static_cast<int>(game::wordAt(owner, kAreaIndex));
        }
        u32 count = game::wordAt(task, kTaskRosterCount);
        for (u32 i = 0; i < count && i < kRosterMax; i++)
        {
            int icon = static_cast<int>(game::wordAt(task, kTaskRoster + 4 * i));
            bool dark = idx >= 0 && !allowed(idx, icon);
            if (dark || s_dark[i])
            {
                tint(task, i, dark);
            }
        }
    }

    void detach(bool toStageSelect)
    {
        undo(s_oneArea);
        undo(s_lock);
        undo(s_recall);
        undo(s_drop);
        undo(s_hover);
        undoGrid();
        tmSelectLockMask = 0;
        allowAll();
        u8* sel = selData();
        if (toStageSelect && !s_reopening && s_built != NULL && s_built->solo && sel != NULL)
        {
            addCpu(sel);
        }
        s_built = NULL;
        s_reopening = false;
    }

    bool needsRebuild(int drill)
    {
        return drills::selectRuleOf(drill) != s_built;
    }

    struct Word {
        u32 address;
        u32 value;
    };
    // sqTraining::setNext (0x806F1510): state 3 opens the select
    // (setNextScene scSelctCharacter, mode byte 10) and moves to state 4.
    static const Word kReopenPath[] = {
        {0x807038C0, 0x806F15E0}, // jump table, state 3
        {0x806F1568, 0x3B200004}, // li r25,4
        {0x806F1620, 0x932F0008}  // stw r25,0x8(r15)     next state 4
    };

    const char* reopen()
    {
        for (u32 i = 0; i < sizeof(kReopenPath) / sizeof(kReopenPath[0]); i++)
        {
            if (codeAt(kReopenPath[i].address) != kReopenPath[i].value)
            {
                return "sqTraining is not the expected code";
            }
        }
        if (!training::selectRunning())
        {
            return "the character select is not running";
        }
        // As training::startMatch leaves the select, with state 3 next. Exit
        // code 1: detach keeps the active session selection.
        gfSceneManager* m = gfSceneManager::getInstance();
        *reinterpret_cast<u32*>(reinterpret_cast<u8*>(m->m_currentSequence) + 0x8) = 3;
        m->unk1 = 1;
        m->processStep = 2;
        s_reopening = true;
        return NULL;
    }

    bool canBackToBootMenu(int port)
    {
        CssBackState st;
        st.selectRunning = training::selectRunning();
        st.areaValid = false;
        st.playerKind = 0;
        st.controllerPort = -1;
        st.handHoldingAny = false;
        st.handHoldingOwn = false;
        st.ownCoinPlaced = false;
        st.anyDropdownOpen = false;
        if (!st.selectRunning)
        {
            return false;
        }
        gfSceneManager* m = gfSceneManager::getInstance();
        if (!isRam(m) || !isRam(m->m_currentScene))
        {
            return false;
        }
        u8* task = reinterpret_cast<u8*>(game::wordAt(m->m_currentScene, kSceneTask));
        if (!isRam(task))
        {
            return false;
        }
        // Check all active player areas for open tag/name/CPU dropdowns
        // (muSelCharHand::m_mode at +0xA4: 0x80689DEC, 0x8068A040, 0x8069E088).
        u32 areaCount = game::wordAt(task, kTaskAreaCount);
        if (areaCount > 4)
        {
            areaCount = 4;
        }
        for (u32 i = 0; i < areaCount; i++)
        {
            u8* a = areaAt(task, static_cast<int>(i));
            u8* h = a != NULL ? reinterpret_cast<u8*>(game::wordAt(a, kAreaHand)) : NULL;
            if (isRam(h))
            {
                // m_mode 0 = idle, 4 = hovering corner Back button (0x8068AAA4);
                // 1..3 are coin animations and 5..8 are dropdowns/submenus.
                u32 mode = game::wordAt(h, kHandMode);
                if (mode != 0 && mode != 4)
                {
                    st.anyDropdownOpen = true;
                }
            }
        }
        u8* p1 = areaAt(task, kAreaHuman);
        u8* hand = p1 != NULL ? reinterpret_cast<u8*>(game::wordAt(p1, kAreaHand)) : NULL;
        u8* coin = p1 != NULL ? reinterpret_cast<u8*>(game::wordAt(p1, kAreaCoin)) : NULL;
        if (!isRam(p1) || !isRam(hand) || !isRam(coin))
        {
            return false;
        }
        st.areaValid = true;
        st.playerKind = static_cast<int>(game::wordAt(p1, kAreaPlayerKind));
        st.controllerPort = static_cast<int>(game::wordAt(p1, kAreaControllerNo));
        u8* heldCoin = reinterpret_cast<u8*>(game::wordAt(hand, kHandCoin));
        u8* coinHolder = reinterpret_cast<u8*>(game::wordAt(coin, kCoinHand));
        st.handHoldingAny = isRam(heldCoin);
        st.handHoldingOwn = isRam(heldCoin) && heldCoin == coin;
        st.ownCoinPlaced = !isRam(heldCoin) && !isRam(coinHolder);
        return canBackFromCss(st, port);
    }

    void clearBHoldCounter()
    {
        gfSceneManager* m = gfSceneManager::getInstance();
        if (!isRam(m) || !isRam(m->m_currentScene) || !training::selectRunning())
        {
            return;
        }
        u8* task = reinterpret_cast<u8*>(game::wordAt(m->m_currentScene, kSceneTask));
        if (!isRam(task))
        {
            return;
        }
        u32 areaCount = game::wordAt(task, kTaskAreaCount);
        if (areaCount > 4)
        {
            areaCount = 4;
        }
        for (u32 i = 0; i < areaCount; i++)
        {
            u8* a = areaAt(task, static_cast<int>(i));
            u8* h = a != NULL ? reinterpret_cast<u8*>(game::wordAt(a, kAreaHand)) : NULL;
            if (isRam(h))
            {
                *reinterpret_cast<u32*>(h + kHandBButtonCnt) = 0;
            }
        }
    }
}
}
}
