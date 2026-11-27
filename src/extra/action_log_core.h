#pragma once

#include <cstdio>
#include <string.h>
#include <types.h>

#include "main/game/status_ids.h"

// Pure host-testable Live State Strip (Action Log) ring buffer, state-class
// classifier, and compact label formatter (zero Brawl headers).
namespace tm {
namespace extra {
    enum {
        kActionLogCap = 8,
        kActionLogLabelSize = 10,
        kActionLogMaxFrames = 999,
        kActionLogDebugMagic = 0x544D414Cu, // "TMAL"
        kActionLogDebugVersion = 2,
        kSf6MaxCells = 32,
        kSf6Cells16 = 32,
        kSf6Cells43 = 24
    };

    // SF6-style per-frame combat phase for the dual frame strip.
    enum Sf6CellKind {
        kSf6Empty = 0,    // idle / actionable
        kSf6Startup = 1,  // attack startup / jumpsquat (teal)
        kSf6Active = 2,   // active hitbox / grab (vivid crimson)
        kSf6Recovery = 3, // non-actionable recovery / landing / dodge endlag (cool slate)
        kSf6Stun = 4,     // hitstun / shieldstun / hitlag / grabbed (Solo-Gold)
        kSf6Gap = 5,      // actionable frame during an active exchange gap (yellow bar)
        kSf6CellKindCount
    };

    struct Sf6StripState {
        u8 cells[2][kSf6MaxCells]; // ring buffer [0=P1, 1=CPU], oldest..newest via head
        u8 head;                   // next write index in 0..kSf6MaxCells-1
        u8 filled;                 // 0..kSf6MaxCells
        u8 sawActive[2];
        s16 prevStatus[2];
        s16 advantage;             // signed frame advantage (+ = P1 ahead, - = CPU ahead)
        u8 hasAdvantage;
        u8 activeExchange;
        s32 exchangeFrame;
        s32 actFrame[2];
        s32 lastFrame;
    };

    enum ActionStateClass {
        kStateNeutral = 0, // neutral / grounded movement
        kStateAttack,      // normals, aerials, specials, grabs/throws
        kStateDefense,     // shield, dodges, rolls, techs
        kStateHitstun,     // damage, hitstun, tumble, knockdown
        kStateLedgeAir,    // jumps, fall, ledge, teeter
        kStateClassCount
    };

    struct ActionLogEntry {
        s16 status;
        s16 motionKind;
        u16 frames;
        u8 stateClass;
        u8 pad;
        char label[kActionLogLabelSize];
    };

    struct ActionLogState {
        ActionLogEntry entries[kActionLogCap];
        u8 head;  // index of oldest entry when count == kActionLogCap
        u8 count; // 0..kActionLogCap
        u8 activeTarget;
        u8 pad;
        u32 totalTransitions;
        s32 lastFrame;
    };

    // RAM debug block exposed for live Dolphin verification ("TMAL").
    // Preserves offsets 0x00..0xA7 verbatim for existing telemetry readers and
    // appends SF6 Dual Frame Strip state at +0xA8.
    struct ActionLogDebugBlock {
        u32 magic;            // 0x00 "TMAL"
        u32 version;          // 0x04 = 2
        u8 targetMode;        // 0x08 0=Off, 1=P1, 2=CPU
        u8 trackedSlot;       // 0x09 0xFF if none
        u8 count;             // 0x0A active ring count (0..8)
        u8 pad;               // 0x0B
        u32 totalTransitions; // 0x0C
        u32 drawCalls;        // 0x10
        s32 lastFrame;        // 0x14
        ActionLogEntry entries[kActionLogCap]; // 0x18 chronological oldest..newest (8 * 18 = 144 bytes)
        s16 sf6Advantage;     // 0xA8 live / last frame advantage
        u8 sf6HasAdvantage;   // 0xAA
        u8 sf6VisibleCells;   // 0xAB 32 on 16:9, 24 on 4:3
        u8 sf6RelocatedTop;   // 0xAC 1 when 3P/4P relocated to top-center
        u8 sf6Filled;         // 0xAD 0..32
        u8 sf6Pad[2];         // 0xAE
        u8 sf6Cells[2][kSf6MaxCells]; // 0xB0 chronological oldest..newest
    };

    inline void sf6ResetState(Sf6StripState& s)
    {
        memset(&s, 0, sizeof(s));
        s.prevStatus[0] = -1;
        s.prevStatus[1] = -1;
        s.actFrame[0] = -1;
        s.actFrame[1] = -1;
        s.exchangeFrame = -1;
        s.lastFrame = -1;
    }

    inline u8 actionLogClassify(int status, bool airborne);

    // True only when this fighter is a DEFENDER in hitstun, shieldstun, grab
    // capture, or victim hitlag (never actionable DamageFall 0x49, grounded
    // Down* 0x4A..0x55 with hitstun == 0, or attacker hitlag on a 3rd/4th player).
    inline bool sf6IsDefenderStun(int status, s16 hitstun, s16 shieldStun, u8 hitReceived, u8 shieldReceived)
    {
        bool stunStatus = status == game::kStatusGuardDamage
                          || (status >= 0x3D && status <= 0x48)
                          || (status >= 0x56 && status <= 0x5F)
                          || (status >= 0x6E && status <= 0x70);
        return hitstun > 0 || shieldStun > 0 || hitReceived != 0 || shieldReceived != 0 || stunStatus;
    }

    inline u8 sf6ClassifyCell(int status, bool actionable, bool activeHitbox, bool inStun, bool sawActive)
    {
        if (inStun)
        {
            return kSf6Stun;
        }
        if (activeHitbox)
        {
            return kSf6Active;
        }
        if (!actionable)
        {
            u8 cls = actionLogClassify(status, false);
            if ((cls == kStateAttack && !sawActive) || status == game::kStatusJumpSquat
                || status == game::kStatusGuardOn)
            {
                return kSf6Startup;
            }
            return kSf6Recovery;
        }
        return kSf6Empty;
    }

    inline u32 sf6CellColor(u8 kind)
    {
        static const u32 kCols[kSf6CellKindCount] = {
            0x1E2A36DCu, // Empty (crisp visible dark slot well)
            0x28D8ECFFu, // Startup (bright cyan-teal)
            0xF43C4EFFu, // Active (vivid crimson)
            0x7698BCFFu, // Recovery (clear steel-blue)
            0xFFB824FFu, // Stun (rich Solo-Gold)
            0x1E2A36DCu  // Gap (dark slot + yellow 3.5px center marker drawn on top)
        };
        return kCols[kind < kSf6CellKindCount ? kind : 0];
    }

    inline u8 sf6CellAt(const Sf6StripState& s, int row, int chronoIdx)
    {
        if (row < 0 || row > 1 || chronoIdx < 0 || chronoIdx >= static_cast<int>(s.filled))
        {
            return kSf6Empty;
        }
        int start = s.filled < kSf6MaxCells ? 0 : static_cast<int>(s.head);
        int idx = (start + chronoIdx) & (kSf6MaxCells - 1);
        return s.cells[row][idx];
    }

    inline void sf6PushDualFrame(Sf6StripState& s, s32 frame, int st0, bool act0, bool hit0, bool stun0,
                                 int st1, bool act1, bool hit1, bool stun1)
    {
        if (frame == s.lastFrame)
        {
            return;
        }
        s.lastFrame = frame;

        if (st0 != s.prevStatus[0])
        {
            s.sawActive[0] = 0;
            s.prevStatus[0] = static_cast<s16>(st0);
        }
        if (st1 != s.prevStatus[1])
        {
            s.sawActive[1] = 0;
            s.prevStatus[1] = static_cast<s16>(st1);
        }

        u8 c0 = sf6ClassifyCell(st0, act0, hit0, stun0, s.sawActive[0] != 0);
        u8 c1 = sf6ClassifyCell(st1, act1, hit1, stun1, s.sawActive[1] != 0);
        if (c0 == kSf6Active)
        {
            s.sawActive[0] = 1;
        }
        if (c1 == kSf6Active)
        {
            s.sawActive[1] = 1;
        }

        // Track exchange frame advantage (positive = P1 acts before CPU, negative = CPU acts first).
        if (stun0 || stun1)
        {
            if (!s.activeExchange || (stun0 && s.actFrame[0] >= 0) || (stun1 && s.actFrame[1] >= 0))
            {
                s.activeExchange = 1;
                s.exchangeFrame = frame;
                s.actFrame[0] = act0 ? frame : -1;
                s.actFrame[1] = act1 ? frame : -1;
            }
        }
        if (s.activeExchange)
        {
            if (s.actFrame[0] < 0 && act0)
            {
                s.actFrame[0] = frame;
            }
            if (s.actFrame[1] < 0 && act1)
            {
                s.actFrame[1] = frame;
            }
            if (s.actFrame[0] >= 0 && s.actFrame[1] >= 0)
            {
                s32 diff = s.actFrame[1] - s.actFrame[0];
                if (diff > -60 && diff < 60)
                {
                    s.advantage = static_cast<s16>(diff);
                    s.hasAdvantage = 1;
                }
                s.activeExchange = 0;
            }
            else if (s.actFrame[0] >= 0 && !act1)
            {
                s32 diff = frame - s.actFrame[0] + 1;
                if (diff < 60)
                {
                    s.advantage = static_cast<s16>(diff);
                    s.hasAdvantage = 1;
                }
                if (c0 == kSf6Empty)
                {
                    c0 = kSf6Gap;
                }
            }
            else if (s.actFrame[1] >= 0 && !act0)
            {
                s32 diff = -(frame - s.actFrame[1] + 1);
                if (diff > -60)
                {
                    s.advantage = static_cast<s16>(diff);
                    s.hasAdvantage = 1;
                }
                if (c1 == kSf6Empty)
                {
                    c1 = kSf6Gap;
                }
            }
            else if (frame - s.exchangeFrame > 180)
            {
                s.activeExchange = 0;
            }
        }

        s.cells[0][s.head] = c0;
        s.cells[1][s.head] = c1;
        s.head = static_cast<u8>((s.head + 1) & (kSf6MaxCells - 1));
        if (s.filled < kSf6MaxCells)
        {
            s.filled++;
        }
    }

    struct ActionPillPalette {
        u32 border;
        u32 fillTop;
        u32 fillBot;
        u32 text;
    };

    inline const ActionPillPalette& actionPillPalette(u8 stateClass)
    {
        static const ActionPillPalette kPal[kStateClassCount] = {
            {0x4CC2D8FFu, 0x1B5464EEu, 0x0F323CEEu, 0xEBF8FFFFu},
            {0xFF6B5EFFu, 0x8A2628EEu, 0x561418EEu, 0xFFF2EEFFu},
            {0x68B0FFFFu, 0x224B8AEEu, 0x122C54EEu, 0xECF6FFFFu},
            {0xFFBE44FFu, 0x8A5614EEu, 0x56340AEEu, 0xFFF8E0FFu},
            {0x52E29AFFu, 0x186446EEu, 0x0C3C2AEEu, 0xEBFFF4FFu}
        };
        return kPal[stateClass < kStateClassCount ? stateClass : 0];
    }

    inline void actionLogResetState(ActionLogState& st)
    {
        memset(&st, 0, sizeof(st));
        st.lastFrame = -1;
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline void actionLogCopyLabel(char out[kActionLogLabelSize], const char* src, int maxLen = kActionLogLabelSize - 1)
    {
        if (src == NULL || src[0] == '\0')
        {
            out[0] = '-';
            out[1] = '\0';
            return;
        }
        int i = 0;
        for (; i < maxLen && src[i] != '\0'; i++)
        {
            out[i] = src[i];
        }
        out[i] = '\0';
    }

    // Classifies a Brawl/P+ fighter status into one of 5 visual pill classes.
    inline u8 actionLogClassify(int status, bool airborne)
    {
        if ((status >= game::kStatusGuardOn && status <= game::kStatusEscapeAir)
            || (status >= 0x60 && status <= 0x64))
        {
            return kStateDefense;
        }
        if ((status >= 0x3D && status <= 0x5F) || (status >= 0x6E && status <= 0x70))
        {
            return kStateHitstun;
        }
        if ((status >= game::kStatusAttack && status <= 0x3C) || status >= game::kStatusSpecialN)
        {
            return kStateAttack;
        }
        if ((status >= game::kStatusJumpSquat && status <= 0x10)
            || status == game::kStatusLandingFallSpecial
            || (status >= 0x67 && status <= 0x6D)
            || (status >= 0x71 && status <= 0x7D)
            || airborne)
        {
            return kStateLedgeAir;
        }
        return kStateNeutral;
    }

    // Produces a crisp <= 7-char label for CRT readability from status + subaction name.
    inline void actionLogShortLabel(int status, const char* statusName, const char* motionName,
                                    char out[kActionLogLabelSize])
    {
        if (status == game::kStatusAttackAirFirst)
        {
            if (motionName != NULL && strncmp(motionName, "AttackAir", 9) == 0)
            {
                static const char kKeys[5] = {'F', 'B', 'N', 'H', 'L'};
                static const char kAir[5][5] = {"Fair", "Bair", "Nair", "Uair", "Dair"};
                char c = motionName[9];
                for (int i = 0; i < 5; i++)
                {
                    if (c == kKeys[i])
                    {
                        actionLogCopyLabel(out, kAir[i]);
                        return;
                    }
                }
            }
            actionLogCopyLabel(out, "AirAtk");
            return;
        }
        if (status >= game::kStatusSpecialN && status <= 0x115)
        {
            static const char kSpec[4][7] = {"SpecN", "SpecS", "SpecHi", "SpecLw"};
            actionLogCopyLabel(out, kSpec[status - game::kStatusSpecialN]);
            return;
        }
        if (status >= 0 && status <= 0x7B)
        {
            struct ByteRange {
                u8 lo;
                u8 hi;
                char name[6];
            };
            static const ByteRange kMap[] = {
                {0x00, 0x00, "Wait"},  {0x01, 0x02, "Walk"},  {0x03, 0x03, "Dash"},
                {0x04, 0x05, "Run"},   {0x06, 0x09, "Turn"},  {0x0A, 0x0A, "JmpSq"},
                {0x0B, 0x0B, "Jump"},  {0x0C, 0x0D, "DJump"}, {0x0E, 0x10, "Fall"},
                {0x11, 0x15, "Squat"}, {0x16, 0x17, "Land"},  {0x18, 0x18, "LandA"},
                {0x19, 0x19, "LandS"}, {0x1A, 0x1C, "Guard"}, {0x1D, 0x1D, "ShStn"},
                {0x1E, 0x1E, "Dodge"}, {0x1F, 0x1F, "RollF"}, {0x20, 0x20, "RollB"},
                {0x21, 0x21, "AirDg"}, {0x24, 0x25, "Jab"},   {0x26, 0x26, "DashA"},
                {0x27, 0x27, "Ftilt"}, {0x28, 0x28, "Utilt"}, {0x29, 0x29, "Dtilt"},
                {0x2A, 0x2C, "FSmsh"}, {0x2D, 0x2F, "DSmsh"}, {0x30, 0x32, "USmsh"},
                {0x34, 0x3B, "Grab"},  {0x3C, 0x3C, "Throw"}, {0x3D, 0x42, "Grabd"},
                {0x43, 0x48, "Hitst"}, {0x49, 0x49, "Tumbl"}, {0x4A, 0x55, "Down"},
                {0x56, 0x5F, "Hitst"}, {0x60, 0x64, "Tech"},  {0x72, 0x72, "Pass"},
                {0x73, 0x7B, "Ledge"}
            };
            for (u32 i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++)
            {
                if (status >= kMap[i].lo && status <= kMap[i].hi)
                {
                    actionLogCopyLabel(out, kMap[i].name);
                    return;
                }
            }
        }
        const char* fallback = (motionName != NULL && motionName[0] != '\0') ? motionName : statusName;
        if (fallback != NULL && fallback[0] != '\0')
        {
            actionLogCopyLabel(out, fallback, 7);
            return;
        }
        snprintf(out, kActionLogLabelSize, "0x%02X", status & 0xFFFF);
    }

    inline const ActionLogEntry* actionLogAt(const ActionLogState& st, int idx)
    {
        if (idx < 0 || idx >= static_cast<int>(st.count))
        {
            return NULL;
        }
        return &st.entries[(static_cast<int>(st.head) + idx) & (kActionLogCap - 1)];
    }

    inline void actionLogPushFrame(ActionLogState& st, s32 frame, int status, int motionKind, bool airborne,
                                   const char* statusName, const char* motionName)
    {
        if (frame == st.lastFrame)
        {
            return;
        }
        st.lastFrame = frame;

        char label[kActionLogLabelSize];
        actionLogShortLabel(status, statusName, motionName, label);
        u8 cls = actionLogClassify(status, airborne);

        if (st.count > 0)
        {
            ActionLogEntry& cur = st.entries[(static_cast<int>(st.head) + static_cast<int>(st.count) - 1)
                                             & (kActionLogCap - 1)];
            if (cur.status == static_cast<s16>(status) && strcmp(cur.label, label) == 0)
            {
                if (cur.frames < kActionLogMaxFrames)
                {
                    cur.frames++;
                }
                return;
            }
        }

        int slot;
        if (st.count < kActionLogCap)
        {
            slot = (static_cast<int>(st.head) + static_cast<int>(st.count)) & (kActionLogCap - 1);
            st.count++;
        }
        else
        {
            slot = static_cast<int>(st.head);
            st.head = static_cast<u8>((st.head + 1) & (kActionLogCap - 1));
        }

        ActionLogEntry& dst = st.entries[slot];
        dst.status = static_cast<s16>(status);
        dst.motionKind = static_cast<s16>(motionKind);
        dst.frames = 1;
        dst.stateClass = cls;
        dst.pad = 0;
        memcpy(dst.label, label, sizeof(dst.label));
        st.totalTransitions++;
    }

    inline void actionLogFormatPill(const ActionLogEntry& e, char* out, u32 size)
    {
        snprintf(out, size, "%s %uf", e.label, static_cast<unsigned>(e.frames));
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif
}
}
