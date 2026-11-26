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
        kActionLogDebugVersion = 1
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
    struct ActionLogDebugBlock {
        u32 magic;            // 0x00 "TMAL"
        u32 version;          // 0x04 = 1
        u8 targetMode;        // 0x08 0=Off, 1=P1, 2=CPU
        u8 trackedSlot;       // 0x09 0xFF if none
        u8 count;             // 0x0A active ring count (0..8)
        u8 pad;               // 0x0B
        u32 totalTransitions; // 0x0C
        u32 drawCalls;        // 0x10
        s32 lastFrame;        // 0x14
        ActionLogEntry entries[kActionLogCap]; // 0x18 chronological oldest..newest
    };

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
