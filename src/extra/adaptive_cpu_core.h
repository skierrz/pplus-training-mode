#pragma once

#include <string.h>
#include <types.h>

#include "extra/net_alloc.h"
#include "main/cpu/cpu_settings.h"

// Pure host-testable adaptive CPU backend (zero Brawl headers,
// zero heap allocation - static/stack storage only).
//
// Algorithms adapted with express permission and credit from fudgepop01's
// SuperTraining MovementTracker and PatternManager (ProjectMCodes
// Codes/SuperTraining/MovementTracker.*, PatternManager.h), with two upstream
// PatternManager bugs fixed:
//   1. Wrap check uses `nextIdx >= kPatternLen` (15) instead of `>= kPatternLen - 1`
//      so slot 14 is never skipped.
//   2. `runCalc` starts at `(nextIdx + kPatternLen - 1) % kPatternLen` (the
//      most recently written entry) instead of the unwritten next slot.
namespace tm {
namespace extra {
    enum MovCategory {
        kMovNone = 0,
        kMovIdle = 1,
        kMovMissedTech = 2,
        kMovRun = 3,
        kMovDash = 4,
        kMovHitstun = 5,
        kMovCrouch = 6,
        kMovJump = 7,
        kMovDjump = 8,
        kMovFall = 9,
        kMovShield = 10,
        kMovAirdodge = 11,
        kMovRoll = 12,
        kMovTech = 13,
        kMovAttack = 14,
        kMovGrab = 15,
        kMovCount = 16
    };

    enum RpsOption {
        kRpsNull = 0,
        kRpsAttack = 1,
        kRpsGrab = 2,
        kRpsDefend = 3
    };

    enum AdaptiveBiasAction {
        kBiasNone = 0,
        kBiasShield = 1,
        kBiasSpotdodge = 2,
        kBiasGrab = 3,
        kBiasJab = 4,
        kBiasJump = 5
    };

    enum {
        kMovHistoryLen = 128,
        kMovLookBack = 38, // ~30% of kMovHistoryLen
        kPatternLen = 15,
        kPatternPlayers = 4
    };

    static const float kAdaptiveConfidenceThreshold = 0.55f;

    // Maps Project+ 3.2.1 fighter status IDs (BrawlHeaders ft/fighter.h) to
    // MovementTracker's 16 movement/action categories.
    inline u8 classifyStatusToMov(int status)
    {
        if (status >= 0x112)
        {
            return kMovAttack;
        }
        if (status < 0 || status > 0x91)
        {
            return kMovNone;
        }
        struct StRange { u8 lo, hi, mov; };
        static const StRange kRanges[] = {
            {0x00, 0x02, kMovIdle},    {0x03, 0x03, kMovDash},    {0x04, 0x05, kMovRun},
            {0x06, 0x06, kMovIdle},    {0x07, 0x07, kMovDash},    {0x08, 0x09, kMovRun},
            {0x0A, 0x0B, kMovJump},    {0x0C, 0x0D, kMovDjump},   {0x0E, 0x10, kMovFall},
            {0x11, 0x15, kMovCrouch},  {0x16, 0x19, kMovIdle},    {0x1A, 0x1D, kMovShield},
            {0x1E, 0x20, kMovRoll},    {0x21, 0x21, kMovAirdodge},{0x24, 0x33, kMovAttack},
            {0x34, 0x34, kMovGrab},    {0x36, 0x36, kMovGrab},    {0x38, 0x38, kMovGrab},
            {0x3D, 0x49, kMovHitstun}, {0x4A, 0x50, kMovMissedTech},{0x51, 0x52, kMovRoll},
            {0x53, 0x53, kMovAttack},  {0x54, 0x55, kMovMissedTech},{0x60, 0x64, kMovTech},
            {0x75, 0x75, kMovIdle},    {0x76, 0x76, kMovAttack},  {0x77, 0x78, kMovRoll},
            {0x79, 0x7B, kMovJump},    {0x7F, 0x7F, kMovGrab},    {0x89, 0x8A, kMovMissedTech},
            {0x8D, 0x8D, kMovMissedTech},{0x8E, 0x8E, kMovRoll},  {0x8F, 0x8F, kMovAttack},
            {0x90, 0x91, kMovRoll}
        };
        for (int i = 0; i < static_cast<int>(sizeof(kRanges) / sizeof(kRanges[0])); i++)
        {
            if (status >= kRanges[i].lo && status <= kRanges[i].hi)
            {
                return kRanges[i].mov;
            }
        }
        return kMovNone;
    }

    // Maps a fighter status into an RPS category (Attack / Grab / Defend / Null).
    inline u8 statusToRps(int status)
    {
        u8 mov = classifyStatusToMov(status);
        if (mov == kMovAttack) return kRpsAttack;
        if (mov == kMovGrab) return kRpsGrab;
        if (mov == kMovShield || mov == kMovRoll || mov == kMovAirdodge) return kRpsDefend;
        return kRpsNull;
    }

    inline float movDiffMultiplier(int source, int comparison)
    {
        if (static_cast<u32>(source) >= kMovCount || static_cast<u32>(comparison) >= kMovCount)
        {
            return source == comparison ? 1.0f : 0.0f;
        }
        enum {
            kIdleGrp = (1u << kMovNone) | (1u << kMovIdle) | (1u << kMovMissedTech) | (1u << kMovRun) | (1u << kMovCrouch),
            kAirGrp = (1u << kMovJump) | (1u << kMovDjump) | (1u << kMovFall),
            kRollGrp = (1u << kMovRoll) | (1u << kMovTech)
        };
        static const u16 kFull[kMovCount] = {
            kIdleGrp, kIdleGrp, kIdleGrp, kIdleGrp,
            (1u << kMovDash) | (1u << kMovHitstun), 1u << kMovHitstun, kIdleGrp,
            (1u << kMovJump) | (1u << kMovFall), kAirGrp, kAirGrp,
            1u << kMovShield, 1u << kMovAirdodge, kRollGrp, kRollGrp,
            1u << kMovAttack, 1u << kMovGrab
        };
        static const u16 kHalf[kMovCount] = {
            1u << kMovDash, 1u << kMovDash, 1u << kMovDash, 1u << kMovDash,
            0, 0, 1u << kMovDash,
            1u << kMovDjump, 0, 0,
            0, 0, 1u << kMovShield, 1u << kMovShield,
            0, 0
        };
        u16 bit = static_cast<u16>(1u << comparison);
        if ((kFull[source] & bit) != 0) return 1.0f;
        if ((kHalf[source] & bit) != 0) return 0.5f;
        return 0.0f;
    }

    // 128-entry static circular history computing similarity-weighted Markov
    // action likelihoods (`approxChance`, `approxCommit`) with zero heap usage.
    class MovementTracker {
    public:
        void reset()
        {
            idx_ = 0;
            samples_ = 0;
            weightTotal_ = 0.0f;
            weightsDirty_ = false;
            memset(action_, 0, sizeof(action_));
            memset(duration_, 0, sizeof(duration_));
            memset(yDistFloor_, 0, sizeof(yDistFloor_));
            memset(xPos_, 0, sizeof(xPos_));
            memset(xDist_, 0, sizeof(xDist_));
            memset(attackChance_, 0, sizeof(attackChance_));
            memset(shieldChance_, 0, sizeof(shieldChance_));
            for (int i = 0; i < kMovHistoryLen; i++)
            {
                otherMov_[i] = -1;
            }
            for (int i = 0; i < kMovCount; i++)
            {
                weightCache_[i] = 0.0f;
                chanceCache_[i] = -1.0f;
            }
        }

        bool weightsDirty() const
        {
            return weightsDirty_;
        }

        u8 currentMov() const
        {
            return action_[idx_];
        }

        u16 currentIndex() const
        {
            return idx_;
        }

        u32 sampleCount() const
        {
            return samples_;
        }

        u8 currentDuration() const
        {
            return duration_[idx_];
        }

        // Advances the current action's frame timer. Returns true only when
        // duration wraps at 254 frames and pushes a continuation entry.
        bool incrementTimer()
        {
            if (duration_[idx_] < 255)
            {
                duration_[idx_]++;
            }
            if (duration_[idx_] >= 254 && action_[idx_] != kMovNone)
            {
                pushCategory(action_[idx_]);
                return true;
            }
            return false;
        }

        void trackStatus(int status)
        {
            u8 mov = classifyStatusToMov(status);
            if (mov != kMovNone)
            {
                pushCategory(mov);
            }
        }

        void trackCategory(u8 mov)
        {
            if (mov != kMovNone && mov < kMovCount)
            {
                pushCategory(mov);
            }
        }

        void updateData(u8 yDistFloor, u8 xPos, u8 xDist, u8 atkChance, u8 shdChance, s8 otherMov)
        {
            yDistFloor_[idx_] = yDistFloor;
            xPos_[idx_] = xPos;
            xDist_[idx_] = xDist;
            attackChance_[idx_] = atkChance;
            shieldChance_[idx_] = shdChance;
            otherMov_[idx_] = otherMov;
            samples_++;
            weightsDirty_ = true;
            invalidateCaches();
        }

        void ensureWeights()
        {
            if (!weightsDirty_)
            {
                return;
            }
            weightsDirty_ = false;
            weightTotal_ = 0.0f;
            for (int i = 0; i < kMovCount; i++)
            {
                chanceCache_[i] = -1.0f;
                calcWeight(i);
            }
        }

        // Reads the last computed chance without forcing lazy calcWeight().
        float cachedChance(int movCategory) const
        {
            if (movCategory < 0 || movCategory >= kMovCount)
            {
                return 0.0f;
            }
            if (chanceCache_[movCategory] >= 0.0f)
            {
                return chanceCache_[movCategory];
            }
            float chance = weightTotal_ > 0.0f ? (weightCache_[movCategory] / weightTotal_) : 0.0f;
            if (chance < 0.0f)
            {
                return 0.0f;
            }
            return chance > 1.0f ? 1.0f : chance;
        }

        float approxChance(int movCategory)
        {
            if (movCategory < 0 || movCategory >= kMovCount)
            {
                return 0.0f;
            }
            ensureWeights();
            if (chanceCache_[movCategory] >= 0.0f)
            {
                return chanceCache_[movCategory];
            }
            float chance = weightTotal_ > 0.0f ? (weightCache_[movCategory] / weightTotal_) : 0.0f;
            if (chance < 0.0f)
            {
                chance = 0.0f;
            }
            else if (chance > 1.0f)
            {
                chance = 1.0f;
            }
            chanceCache_[movCategory] = chance;
            return chance;
        }

        float approxCommit()
        {
            ensureWeights();
            if (weightTotal_ <= 0.0f)
            {
                return 0.0f;
            }
            float w = weightCache_[kMovAttack] + weightCache_[kMovGrab];
            float chance = w / weightTotal_;
            if (weightCache_[kMovGrab] < 1.0f)
            {
                chance *= 1.2f;
            }
            if (chance < 0.0f)
            {
                chance = 0.0f;
            }
            else if (chance > 1.0f)
            {
                chance = 1.0f;
            }
            return chance;
        }

    private:
        static u8 categoryWeight(int mov)
        {
            static const u8 kWeights[kMovCount] = {
                20,  // kMovNone
                40,  // kMovIdle
                0,   // kMovMissedTech
                60,  // kMovRun
                120, // kMovDash
                100, // kMovHitstun
                100, // kMovCrouch
                120, // kMovJump
                130, // kMovDjump
                100, // kMovFall
                160, // kMovShield
                80,  // kMovAirdodge
                115, // kMovRoll
                110, // kMovTech
                150, // kMovAttack
                190  // kMovGrab
            };
            return (mov >= 0 && mov < kMovCount) ? kWeights[mov] : 20;
        }

        static int absDiff(int a, int b)
        {
            int d = a - b;
            return d < 0 ? -d : d;
        }

        void invalidateCaches()
        {
            for (int i = 0; i < kMovCount; i++)
            {
                chanceCache_[i] = -1.0f;
            }
        }

        void pushCategory(u8 mov)
        {
            idx_ = static_cast<u16>((idx_ + 1u) % kMovHistoryLen);
            action_[idx_] = mov;
            duration_[idx_] = 0;
            weightsDirty_ = true;
            invalidateCaches();
        }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
        void calcWeight(int actionType)
        {
            int offsets[kMovHistoryLen];
            int offsetCount = 0;
            int tracker = static_cast<int>(idx_);
            for (int i = 0; i < kMovHistoryLen; i++)
            {
                if (tracker < 0)
                {
                    tracker = kMovHistoryLen - 1;
                }
                if (action_[tracker] == kMovNone)
                {
                    break;
                }
                if (action_[tracker] == actionType && tracker != static_cast<int>(idx_))
                {
                    offsets[offsetCount++] = tracker;
                }
                tracker--;
            }

            float score = 0.0f;
            float totalScore = 0.0f;
            static const float kInvTotalFeatureWeight = 1.0f / 25.0f;
            static const float kInvLookBack = 1.0f / static_cast<float>(kMovLookBack);
            const u8* const featArr[6] = {
                duration_, yDistFloor_, xDist_, attackChance_, shieldChance_, xPos_
            };
            static const u8 kFeatLim[6] = {60, 35, 75, 125, 125, 250};
            static const float kFeatCoeff[6] = {
                2.0f / 60.0f, 3.0f / 35.0f, 6.0f / 75.0f, 4.0f / 125.0f, 4.0f / 125.0f, 4.0f / 250.0f
            };

            for (int i = 0; i < offsetCount; i++)
            {
                int startTracker = static_cast<int>(idx_);
                if (action_[startTracker] == actionType)
                {
                    startTracker--;
                }
                int offsetTracker = offsets[i] - 1;
                int looked = 0;
                float scoreMult = 1.0f;

                while (offsetTracker != static_cast<int>(idx_) && looked < kMovLookBack)
                {
                    if (startTracker < 0) startTracker = kMovHistoryLen - 1;
                    if (offsetTracker < 0) offsetTracker = kMovHistoryLen - 1;
                    if (action_[offsetTracker] == kMovNone) break;

                    float toAdd = i2fScale(200 - categoryWeight(action_[offsetTracker]), 0.01f) * scoreMult;
                    totalScore += toAdd;

                    float diffMult = movDiffMultiplier(action_[startTracker], action_[offsetTracker]);
                    if (diffMult > 0.0f)
                    {
                        float sim = otherMov_[startTracker] == otherMov_[offsetTracker] ? 2.0f : 0.0f;
                        for (int f = 0; f < 6; f++)
                        {
                            int d = absDiff(featArr[f][startTracker], featArr[f][offsetTracker]);
                            sim += i2fScale(static_cast<int>(kFeatLim[f]) - d, kFeatCoeff[f]);
                        }
                        float weighted = toAdd * (sim * kInvTotalFeatureWeight);
                        if (weighted > 0.0f)
                        {
                            score += weighted * diffMult;
                        }
                    }

                    looked++;
                    scoreMult = i2fScale(kMovLookBack - looked, kInvLookBack);
                    startTracker--;
                    offsetTracker--;
                }
            }

            float result = totalScore > 0.0f ? (score / totalScore) : 0.0f;
            weightCache_[actionType] = result;
            weightTotal_ += result;
        }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

        u16 idx_;
        u32 samples_;
        u8 action_[kMovHistoryLen];
        u8 duration_[kMovHistoryLen];
        u8 yDistFloor_[kMovHistoryLen];
        u8 xPos_[kMovHistoryLen];
        u8 xDist_[kMovHistoryLen];
        u8 attackChance_[kMovHistoryLen];
        u8 shieldChance_[kMovHistoryLen];
        s8 otherMov_[kMovHistoryLen];
        float weightCache_[kMovCount];
        float weightTotal_;
        float chanceCache_[kMovCount];
        bool weightsDirty_;
    };

    // 15-entry weighted circular RPS history per player (Attack / Grab / Defend).
    // Fixes upstream wrap (slot 14 is now used) and calculation start index
    // (starts at most recently written entry instead of next unwritten slot).
    class PatternManager {
    public:
        void clearAll()
        {
            for (int p = 0; p < kPatternPlayers; p++)
            {
                clear(p);
            }
        }

        void clear(int player)
        {
            if (player < 0 || player >= kPatternPlayers)
            {
                return;
            }
            for (int i = 0; i < kPatternLen; i++)
            {
                history_[player][i] = kRpsNull;
            }
            nextIdx_[player] = 0;
            count_[player] = 0;
        }

        void pushNew(int player, u8 option)
        {
            if (player < 0 || player >= kPatternPlayers)
            {
                return;
            }
            history_[player][nextIdx_[player]] = option;
            nextIdx_[player]++;
            if (nextIdx_[player] >= kPatternLen)
            {
                nextIdx_[player] = 0;
            }
            count_[player]++;
        }

        u32 count(int player) const
        {
            return (player >= 0 && player < kPatternPlayers) ? count_[player] : 0u;
        }

        u8 nextIndex(int player) const
        {
            return (player >= 0 && player < kPatternPlayers) ? nextIdx_[player] : 0u;
        }

        u8 entryAt(int player, int slot) const
        {
            if (player < 0 || player >= kPatternPlayers || slot < 0 || slot >= kPatternLen)
            {
                return kRpsNull;
            }
            return history_[player][slot];
        }

        void runCalc(int player, int lookAmount, int& attackScore, int& grabScore, int& defendScore) const
        {
            attackScore = 0;
            grabScore = 0;
            defendScore = 0;
            if (player < 0 || player >= kPatternPlayers || lookAmount <= 0)
            {
                return;
            }
            if (lookAmount > kPatternLen)
            {
                lookAmount = kPatternLen;
            }
            // Start at the most recently written slot (one behind nextIdx_).
            int pos = (static_cast<int>(nextIdx_[player]) + kPatternLen - 1) % kPatternLen;
            for (int weight = lookAmount; weight > 0; weight--)
            {
                switch (history_[player][pos])
                {
                case kRpsAttack:
                    attackScore += weight;
                    break;
                case kRpsGrab:
                    grabScore += weight;
                    break;
                case kRpsDefend:
                    defendScore += weight;
                    break;
                default:
                    break;
                }
                pos = (pos + kPatternLen - 1) % kPatternLen;
            }
        }

        u8 dominantWithScore(int player, int lookAmount, int& bestOut) const
        {
            int atk = 0, grab = 0, def = 0;
            runCalc(player, lookAmount, atk, grab, def);
            if (atk > grab && atk > def) { bestOut = atk; return kRpsAttack; }
            if (grab > atk && grab > def) { bestOut = grab; return kRpsGrab; }
            if (def > atk && def > grab) { bestOut = def; return kRpsDefend; }
            bestOut = 0;
            return kRpsNull;
        }

        u8 dominantRps(int player, int lookAmount = kPatternLen) const
        {
            int best = 0;
            return dominantWithScore(player, lookAmount, best);
        }

        // Confidence in [0.0, 1.0]: weighted score of the dominant RPS option
        // over the maximum possible triangle sum lookAmount*(lookAmount+1)/2.
        float confidence(int player, int lookAmount = kPatternLen) const
        {
            if (lookAmount <= 0) return 0.0f;
            if (lookAmount > kPatternLen) lookAmount = kPatternLen;
            int best = 0;
            if (dominantWithScore(player, lookAmount, best) == kRpsNull) return 0.0f;
            float ratio = i2f(best) / i2f(lookAmount * (lookAmount + 1) / 2);
            return clampf(ratio, 0.0f, 1.0f);
        }

    private:
        u8 history_[kPatternPlayers][kPatternLen];
        u8 nextIdx_[kPatternPlayers];
        u32 count_[kPatternPlayers];
    };

    // Chooses an adaptive counter bias when adaptiveMode == kAdaptiveActive
    // and the opponent's dominant RPS habit exceeds kAdaptiveConfidenceThreshold.
    //   Attack habit -> Shield / Spotdodge (or Grab OoS)
    //   Grab habit   -> Spotdodge / Jab / Jump
    //   Defend habit -> Grab
    inline u8 chooseAdaptiveBias(u8 adaptiveMode, u8 dominantRps, float confidence, bool inShield, u32 mixCounter)
    {
        if (adaptiveMode != cpu::kAdaptiveActive || confidence < kAdaptiveConfidenceThreshold)
        {
            return kBiasNone;
        }
        switch (dominantRps)
        {
        case kRpsAttack:
            if (inShield)
            {
                return (mixCounter & 1u) == 0u ? kBiasGrab : kBiasSpotdodge;
            }
            return (mixCounter & 1u) == 0u ? kBiasShield : kBiasSpotdodge;
        case kRpsGrab:
            if (inShield)
            {
                return (mixCounter & 1u) == 0u ? kBiasSpotdodge : kBiasJump;
            }
            return (mixCounter % 3u) == 0u ? kBiasSpotdodge : ((mixCounter % 3u) == 1u ? kBiasJab : kBiasJump);
        case kRpsDefend:
            return kBiasGrab;
        default:
            return kBiasNone;
        }
    }

    // Maps an AdaptiveBiasAction to a cpu/actions.cpp index for kListGround (0)
    // or kListShield (2); returns -1 if not applicable.
    inline int adaptiveBiasToActionIndex(u8 biasAction, int list)
    {
        // Indices match src/main/cpu/actions.cpp kGround and kShieldList:
        //   Ground: 1=Spotdodge, 2=Shield, 3=Grab, 13=Jab, 18=Short Hop
        //   Shield: 1=Grab, 2=Short Hop, 4=Spotdodge
        static const s8 kGroundIdx[6] = {-1, 2, 1, 3, 13, 18};
        static const s8 kShieldIdx[6] = {-1, 1, 4, 1, -1, 2};
        if (biasAction >= 6u) return -1;
        if (list == 0) return kGroundIdx[biasAction];
        if (list == 2) return kShieldIdx[biasAction];
        return -1;
    }

    // Resolves an adaptive action index with explicit peek vs commit semantics.
    // When commit == false, returns the candidate action index without mutating
    // mixCounter, cooldown, biasAction, or biasTriggers. When inCounter == false,
    // also enforces engageRange, neutralCooldown, and !inGuard so guarding CPUs
    // wait for shieldstun OoS counters rather than dropping shield in neutral.
    inline int selectAdaptiveAction(u8 adaptiveMode, u8 dominantRps, float confidence, int list, bool inCounter,
                                    bool inGuard, float opponentDist, float engageRange, bool commit, u32& mixCounter,
                                    u16& neutralCooldown, u16 cooldownReset, u32& biasActionOut, u32& biasTriggersOut)
    {
        if (adaptiveMode != cpu::kAdaptiveActive)
        {
            return -1;
        }
        if (!inCounter)
        {
            if (inGuard || neutralCooldown > 0 || opponentDist > engageRange)
            {
                return -1;
            }
        }
        bool inShield = (list == 2); // kListShield
        u8 bias = chooseAdaptiveBias(adaptiveMode, dominantRps, confidence, inShield, mixCounter);
        if (bias == kBiasNone)
        {
            return -1;
        }
        int actionIdx = adaptiveBiasToActionIndex(bias, list);
        if (actionIdx <= 0)
        {
            return -1;
        }
        if (commit)
        {
            mixCounter++;
            if (!inCounter)
            {
                neutralCooldown = cooldownReset;
            }
            biasActionOut = bias;
            biasTriggersOut++;
        }
        return actionIdx;
    }

    // Compact live Habit Readout HUD view model (Track & Adaptive modes).
    struct AdaptiveHudView {
        u8 mode;          // cpu::AdaptiveMode
        u8 atkPct;        // 0..100 weighted RPS Attack share
        u8 grabPct;       // 0..100 weighted RPS Grab share
        u8 defPct;        // 0..100 weighted RPS Defend share
        u8 confidencePct; // 0..100 dominant read confidence
        u8 dominantRps;   // RpsOption
        u8 biasAction;    // AdaptiveBiasAction (live/predicted counter bias)
        bool confident;   // confidence >= kAdaptiveConfidenceThreshold
        u32 rpsCount;     // total RPS samples seen
    };

    inline const char* adaptiveRpsLabel(u8 rps)
    {
        static const char* const kLabels[4] = {"Balanced", "Attack", "Grab", "Defend"};
        return rps < 4u ? kLabels[rps] : kLabels[0];
    }

    inline const char* adaptiveBiasLabel(u8 bias)
    {
        static const char* const kLabels[6] = {"None", "Shield", "Spotdodge", "Grab", "Jab", "Short Hop"};
        return bias < 6u ? kLabels[bias] : kLabels[0];
    }

    inline void adaptiveBuildHudView(u8 mode, const PatternManager& patterns, int player, u8 lastBiasAction,
                                     AdaptiveHudView& out)
    {
        out.mode = mode;
        out.atkPct = 0;
        out.grabPct = 0;
        out.defPct = 0;
        out.confidencePct = 0;
        out.dominantRps = kRpsNull;
        out.biasAction = kBiasNone;
        out.confident = false;
        out.rpsCount = patterns.count(player);
        if (mode == cpu::kAdaptiveOff)
        {
            return;
        }
        int atk = 0, grab = 0, def = 0;
        patterns.runCalc(player, kPatternLen, atk, grab, def);
        int total = atk + grab + def;
        if (total > 0)
        {
            int ap = (atk * 100 + total / 2) / total;
            int gp = (grab * 100 + total / 2) / total;
            int dp = 100 - ap - gp;
            if (dp < 0)
            {
                if (ap >= gp)
                {
                    ap += dp;
                }
                else
                {
                    gp += dp;
                }
                dp = 0;
            }
            out.atkPct = static_cast<u8>(ap);
            out.grabPct = static_cast<u8>(gp);
            out.defPct = static_cast<u8>(dp);
        }
        out.dominantRps = patterns.dominantRps(player);
        float conf = patterns.confidence(player);
        int cp = f2i(conf * 100.0f + 0.5f);
        if (cp < 0)
        {
            cp = 0;
        }
        else if (cp > 100)
        {
            cp = 100;
        }
        out.confidencePct = static_cast<u8>(cp);
        out.confident = conf >= kAdaptiveConfidenceThreshold;
        if (mode == cpu::kAdaptiveActive && out.confident)
        {
            out.biasAction = lastBiasAction != kBiasNone
                                 ? lastBiasAction
                                 : chooseAdaptiveBias(mode, out.dominantRps, conf, false, 0u);
        }
    }
}
}
