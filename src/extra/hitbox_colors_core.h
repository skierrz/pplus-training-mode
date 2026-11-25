#pragma once

#include <types.h>

#include "main/settings.h"

// Pure P+ 3.2.1 Hitbox Property and Priority Color classifier.
// Verified against P+ 3.2.1 soCollisionAttackPart (0x90 B) and Constants.asm:
//  - +0x00 m_status (2 = Active)
//  - +0x04 m_power (s32 damage)
//  - +0x18 m_vector (s32 angle; P+ 3.2.1 Meteor = inclusive 260..280)
//  - +0x38 flags: m_setOffKind = (w >> 30) & 3 (0 = Transcendent),
//                 m_isShieldable = (w >> 21) & 1 (0 = Unblockable)
//  - +0x88 m_groupIndex
namespace tm {
namespace extra {
    enum HitboxProperty {
        kHitPropNormal = 0,
        kHitPropSweetspot,
        kHitPropSourspot,
        kHitPropMeteor,
        kHitPropTranscendent,
        kHitPropUnblockable,
        kHitPropGrab,
        kHitPropCount
    };

    enum {
        kPPlusMeteorMin = 260,
        kPPlusMeteorMax = 280,
        kHitboxColorNormal = 0xEF4444B8u,
        kHitboxColorSweetspot = 0xF59E0BB8u,
        kHitboxColorSourspot = 0x60A5FAB8u,
        kHitboxColorMeteor = 0xA855F7B8u,
        kHitboxColorTranscendent = 0x22D3EEB8u,
        kHitboxColorUnblockable = 0xFF1E64B8u,
        kHitboxColorGrab = 0x22C55EB8u
    };

    struct HitboxPartProps {
        s32 power;
        s32 angle;
        u8 setOffKind; // (flags >> 30) & 3; 0 = Transcendent (SetOff_Off)
        u8 shieldable; // (flags >> 21) & 1; 0 = Unblockable
    };

    inline HitboxPartProps decodeAttackPartFlags(s32 power, s32 angle, u32 flags38)
    {
        HitboxPartProps p;
        p.power = power;
        p.angle = angle;
        p.setOffKind = static_cast<u8>((flags38 >> 30) & 3u);
        p.shieldable = static_cast<u8>((flags38 >> 21) & 1u);
        return p;
    }

    inline bool isPPlusMeteorAngle(s32 angle)
    {
        return angle >= kPPlusMeteorMin && angle <= kPPlusMeteorMax;
    }

    // Classifies one active attack hitbox part into its highest-signal P+ property.
    inline HitboxProperty classifyHitboxProperty(const HitboxPartProps& p, s32 minPower, s32 maxPower)
    {
        if (p.shieldable == 0)
        {
            return kHitPropUnblockable;
        }
        if (isPPlusMeteorAngle(p.angle))
        {
            return kHitPropMeteor;
        }
        if (p.setOffKind == 0)
        {
            return kHitPropTranscendent;
        }
        if (maxPower > minPower)
        {
            return (p.power >= maxPower) ? kHitPropSweetspot : kHitPropSourspot;
        }
        return kHitPropNormal;
    }

    inline u32 hitboxPropertyColor(HitboxProperty prop)
    {
        switch (prop)
        {
        case kHitPropUnblockable:
            return kHitboxColorUnblockable;
        case kHitPropMeteor:
            return kHitboxColorMeteor;
        case kHitPropTranscendent:
            return kHitboxColorTranscendent;
        case kHitPropSweetspot:
            return kHitboxColorSweetspot;
        case kHitPropSourspot:
            return kHitboxColorSourspot;
        case kHitPropGrab:
            return kHitboxColorGrab;
        default:
            return kHitboxColorNormal;
        }
    }

    // High-contrast distinct palette by hitbox priority index (id 0 = highest).
    inline u32 hitboxPriorityColor(int id)
    {
        static const u32 kPalette[8] = {
            0xF59E0BB8u, // id 0: gold
            0xEF4444B8u, // id 1: red
            0x22D3EEB8u, // id 2: cyan
            0x84CC16B8u, // id 3: lime
            0xA855F7B8u, // id 4: violet
            0xFB923CB8u, // id 5: orange
            0xEC4899B8u, // id 6: pink
            0x38BDF8B8u  // id 7: sky
        };
        int idx = id >= 0 ? (id & 7) : 0;
        return kPalette[idx];
    }

    inline u32 hitboxColorForMode(u8 mode, int id, const HitboxPartProps& props, s32 minPower, s32 maxPower)
    {
        if (mode == main::kHitboxColorPriority)
        {
            return hitboxPriorityColor(id);
        }
        return hitboxPropertyColor(classifyHitboxProperty(props, minPower, maxPower));
    }

    inline u32 hitboxShadeColor(u32 rgba)
    {
        u32 r = ((rgba >> 24) & 0xFFu) * 3u / 5u;
        u32 g = ((rgba >> 16) & 0xFFu) * 3u / 5u;
        u32 b = ((rgba >> 8) & 0xFFu) * 3u / 5u;
        u32 a = rgba & 0xFFu;
        return (r << 24) | (g << 16) | (b << 8) | a;
    }

    inline void hitboxIdentityMatrix34(float m[3][4])
    {
        m[0][0] = 1.0f;
        m[0][1] = 0.0f;
        m[0][2] = 0.0f;
        m[0][3] = 0.0f;
        m[1][0] = 0.0f;
        m[1][1] = 1.0f;
        m[1][2] = 0.0f;
        m[1][3] = 0.0f;
        m[2][0] = 0.0f;
        m[2][1] = 0.0f;
        m[2][2] = 1.0f;
        m[2][3] = 0.0f;
    }
}
}
