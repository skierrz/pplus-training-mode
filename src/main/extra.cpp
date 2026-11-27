#include "main/extra.h"

#include <OS/OSLink.h>
#include <stddef.h>

#include "main/savestate/snapshot.h"
#include "main/ui/info_display.h"
#include "shared/game/mem.h"
#include "shared/log.h"
#include "shared/perf.h"

#ifndef TM_STUB_MODULE_ID
#define TM_STUB_MODULE_ID 21581
#endif

namespace tm {
namespace main {
    static const ExtraHooks* s_hooks = NULL;

    // Exclude the extra module's gfMemoryPool payload [block, block + payloadLen)
    // from savestate snapshots (slot 1; slot 0 belongs to recording storage).
    // AssistFigureResource is walked by snapshot.cpp, so leaving extra's code,
    // .data, and .bss out keeps the heap snapshot tiny (~192 B) and preserves
    // extra's live state across savestate loads.
    static void syncExtraSavestateExclusion(bool present)
    {
        if (!present)
        {
            savestate::setOwnExclusionAt(1, 0, 0);
            return;
        }
        OSModuleInfo* const* head = reinterpret_cast<OSModuleInfo* const*>(0x800030C8);
        OSModuleInfo* m = *head;
        for (u32 count = 0; game::isRam(m) && count < 64; count++)
        {
            if (m->id == TM_STUB_MODULE_ID + 3)
            {
                u32 blockAddr = reinterpret_cast<u32>(m);
                const u8* hdr = reinterpret_cast<const u8*>(blockAddr - 0x20);
                if (game::isRam(hdr))
                {
                    u32 total = game::wordAt(hdr, 4);
                    if (total > 0x20 && (total & 0x1F) == 0)
                    {
                        u32 payloadLen = total - 0x20;
                        savestate::setOwnExclusionAt(1, blockAddr, payloadLen);
                        TM_LOG("main: extra savestate exclusion %08x +%x\n", blockAddr, payloadLen);
                    }
                }
                return;
            }
            m = m->link.next;
        }
    }

    // Only the extra module calls this: kept from the linker's stripping,
    // as anything else only the extra module uses must be.
#pragma force_active on
    void setExtraHooks(const ExtraHooks* hooks)
    {
        s_hooks = hooks;
        syncExtraSavestateExclusion(hooks != NULL);
        TM_LOG("main: extra module %s\n", hooks != NULL ? "registered" : "gone");
    }
#pragma force_active reset

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    static void callVoid(u32 off)
    {
        if (s_hooks != NULL)
        {
            typedef void (*Fn)();
            Fn fn = *reinterpret_cast<const Fn*>(reinterpret_cast<const u8*>(s_hooks) + off);
            if (fn != NULL) fn();
        }
    }

    static bool callBool(u32 off)
    {
        if (s_hooks == NULL) return false;
        typedef bool (*Fn)();
        Fn fn = *reinterpret_cast<const Fn*>(reinterpret_cast<const u8*>(s_hooks) + off);
        return fn != NULL && fn();
    }

    static void callMatch(u32 off, const game::MatchState& match)
    {
        if (s_hooks != NULL)
        {
            typedef void (*Fn)(const game::MatchState&);
            Fn fn = *reinterpret_cast<const Fn*>(reinterpret_cast<const u8*>(s_hooks) + off);
            if (fn != NULL) fn(match);
        }
    }

    static void callMatchBool(u32 off, const game::MatchState& match, bool menuOpen)
    {
        if (s_hooks != NULL)
        {
            typedef void (*Fn)(const game::MatchState&, bool);
            Fn fn = *reinterpret_cast<const Fn*>(reinterpret_cast<const u8*>(s_hooks) + off);
            if (fn != NULL) fn(match, menuOpen);
        }
    }

    void extraRender(u32 renderCall, const game::MatchState& match)
    {
        if (s_hooks != NULL && s_hooks->render != NULL) s_hooks->render(renderCall, match);
    }

    bool extraAlterRow() { return callBool(offsetof(ExtraHooks, alterRow)); }
    void extraAlterEdited() { callVoid(offsetof(ExtraHooks, alterEdited)); }
    bool extraLoaded() { return s_hooks != NULL; }

    static bool rngCall(int call)
    {
        return s_hooks != NULL && s_hooks->characterRng != NULL && s_hooks->characterRng(call);
    }

    static bool stageCall(int call)
    {
        return s_hooks != NULL && s_hooks->stageOptions != NULL && s_hooks->stageOptions(call);
    }

    void extraScenarioCall(int call)
    {
        if (s_hooks != NULL && s_hooks->scenarioCall != NULL) s_hooks->scenarioCall(call);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    void extraCharacterRng() { rngCall(kRngApply); }
    bool extraPeachRows() { return rngCall(kRngPeach); }
    bool extraGameWatchRows() { return rngCall(kRngGameWatch); }
    bool extraLuigiRows() { return rngCall(kRngLuigi); }
    bool extraSamusRows() { return rngCall(kRngSamus); }
    bool extraMewtwoRows() { return rngCall(kRngMewtwo); }
    bool extraLucarioRows() { return rngCall(kRngLucario); }
    bool extraWarioRows() { return rngCall(kRngWario); }
    bool extraDonkeyKongRows() { return rngCall(kRngDonkeyKong); }
    bool extraIvysaurRows() { return rngCall(kRngIvysaur); }
    bool extraRobRows() { return rngCall(kRngRob); }
    bool extraLucasRows() { return rngCall(kRngLucas); }
    bool extraSquirtleRows() { return rngCall(kRngSquirtle); }
    bool extraSheikRows() { return rngCall(kRngSheik); }
    bool extraDededeRows() { return rngCall(kRngDedede); }
    bool extraSnakeRows() { return rngCall(kRngSnake); }
    bool extraGanondorfRows() { return rngCall(kRngGanondorf); }
    bool extraIceClimbersRows() { return rngCall(kRngIceClimbers); }
    bool extraOlimarRows() { return rngCall(kRngOlimar); }
    bool extraOlimarP1() { return rngCall(kRngOlimarP1); }
    void extraOlimarAction(u32 action, u32 dpadHeld, u32 dpadPressed)
    {
        if (s_hooks != NULL && s_hooks->olimarAction != NULL)
        {
            s_hooks->olimarAction(action, dpadHeld, dpadPressed);
        }
    }
    bool extraCharacterRngPage() { return rngCall(kRngAny); }

    void extraStageOptionsTick() { stageCall(kStageTick); }
    void extraStageOptionsChanged() { stageCall(kStageChanged); }
    void extraStageOptionsSavestateLoaded() { stageCall(kStageSavestateLoaded); }
    bool extraStageOptionsPage() { return stageCall(kStagePage); }
    bool extraStageHazardsRow() { return stageCall(kStageHazardsRow); }
    bool extraStagePlatformsRow() { return stageCall(kStagePlatformsRow); }
    bool extraStageFodRow() { return stageCall(kStageFodRow); }

    bool extraDiRow() { return s_hooks != NULL; }
    void extraDiDraw(const game::MatchState& match)
    {
        TM_PERF_SCOPE(kDiDraw);
        callMatch(offsetof(ExtraHooks, diDraw), match);
    }
    void extraDiReset() { callVoid(offsetof(ExtraHooks, diReset)); }
    void extraDiHideDrawDi(bool hide)
    {
        if (s_hooks != NULL && s_hooks->diHideDrawDi != NULL) s_hooks->diHideDrawDi(hide);
    }

    void extraHeatmapDraw(const game::MatchState& match) { callMatch(offsetof(ExtraHooks, heatmapDraw), match); }
    void extraHeatmapReset() { callVoid(offsetof(ExtraHooks, heatmapReset)); }
    void extraGrabVizDraw(const game::MatchState& match) { callMatch(offsetof(ExtraHooks, grabVizDraw), match); }

    int extraActIn(Fighter* fighter, int* how)
    {
        *how = kActNone;
        return s_hooks != NULL && s_hooks->actIn != NULL ? s_hooks->actIn(fighter, how) : -1;
    }

    void extraFrameData() { callVoid(offsetof(ExtraHooks, frameData)); }

    bool extraAdvantageMeasured(const game::Advantage& r)
    {
        return s_hooks != NULL && s_hooks->advantageMeasured != NULL && s_hooks->advantageMeasured(r);
    }

    bool extraShieldHoldFullMove(int defenderPlayerNo, int defenderEntryId, const cpu::View& v, bool newHit)
    {
        return s_hooks != NULL && s_hooks->shieldHoldFullMove != NULL
               && s_hooks->shieldHoldFullMove(defenderPlayerNo, defenderEntryId, v, newHit);
    }

    bool extraPPlusRecovery(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out, u32 lastButtons)
    {
        return s_hooks != NULL && s_hooks->pplusRecovery != NULL
               && s_hooks->pplusRecovery(who, v, f, out, lastButtons);
    }

    void extraPPlusRecoveryReset(int slot)
    {
        if (s_hooks != NULL && s_hooks->pplusRecoveryReset != NULL) s_hooks->pplusRecoveryReset(slot);
    }

    void extraComboDraw(const game::MatchState& match) { callMatch(offsetof(ExtraHooks, comboDraw), match); }
    void extraComboReset() { callVoid(offsetof(ExtraHooks, comboReset)); }

    int extraAdaptiveBias(int cpuPlayerNo, int list, bool inCounter, float opponentDist, bool commit)
    {
        return s_hooks != NULL && s_hooks->adaptiveBias != NULL
                   ? s_hooks->adaptiveBias(cpuPlayerNo, list, inCounter, opponentDist, commit)
                   : -1;
    }

    void extraAdaptiveReset() { callVoid(offsetof(ExtraHooks, adaptiveReset)); }
    void extraScenarioChanged() { extraScenarioCall(kScenarioChanged); }
    void extraScenarioResetNow() { extraScenarioCall(kScenarioResetNow); }

    bool extraScenarioCpuStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out, u32 lastButtons)
    {
        return s_hooks != NULL && s_hooks->scenarioCpuStep != NULL
               && s_hooks->scenarioCpuStep(who, v, f, out, lastButtons);
    }

    void extraScenarioSavedView(Settings& out)
    {
        if (s_hooks != NULL && s_hooks->scenarioSavedView != NULL) s_hooks->scenarioSavedView(out);
    }

    bool extraFighterSwapRow() { return callBool(offsetof(ExtraHooks, fighterSwapRow)); }
    void extraFighterSwapChanged() { callVoid(offsetof(ExtraHooks, fighterSwapChanged)); }
    void extraFighterSwapApply() { callVoid(offsetof(ExtraHooks, fighterSwapApply)); }
    bool extraFighterSwapBusy() { return callBool(offsetof(ExtraHooks, fighterSwapBusy)); }

    const char* extraOptionDesc(u8 pageId, u8 row)
    {
        return s_hooks != NULL && s_hooks->optionDesc != NULL ? s_hooks->optionDesc(pageId, row) : NULL;
    }

    void extraDrawPageOverlay(u8 pageId, bool railMode)
    {
        if (s_hooks != NULL && s_hooks->drawPageOverlay != NULL) s_hooks->drawPageOverlay(pageId, railMode);
    }

    bool extraInfoWanted() { return ui::infoWanted(); }
    void extraInfoStep(bool previousRan) { ui::infoStep(previousRan); }
    void extraInfoDraw() { ui::drawInfo(); }
    void extraInfoReset() { ui::infoReset(); }
    void extraInfoApplyPreset(int side) { ui::applyInfoPreset(side); }

    void extraInputViewerDraw(const game::MatchState& match, bool menuOpen)
    {
        callMatchBool(offsetof(ExtraHooks, inputViewerDraw), match, menuOpen);
    }

    bool extraDrawColoredHitboxes(Fighter* fighter)
    {
        return s_hooks != NULL && s_hooks->drawColoredHitboxes != NULL && s_hooks->drawColoredHitboxes(fighter);
    }

    void extraDiCompassDraw(const game::MatchState& match, bool menuOpen)
    {
        callMatchBool(offsetof(ExtraHooks, diCompassDraw), match, menuOpen);
    }

    bool extraReversalPick(int trigger, int fighterKind, u32 roll, int& outAction, int& outDelay)
    {
        return s_hooks != NULL && s_hooks->reversalPick != NULL
               && s_hooks->reversalPick(trigger, fighterKind, roll, outAction, outDelay);
    }

    void extraPsaInspectorDraw(const game::MatchState& match, bool menuOpen)
    {
        callMatchBool(offsetof(ExtraHooks, psaInspectorDraw), match, menuOpen);
    }

    void extraTimingMeterDraw(const game::MatchState& match)
    {
        callMatch(offsetof(ExtraHooks, timingMeterDraw), match);
    }

    u32 extraDrill(u32 phase, gfPadStatus* pads)
    {
        return s_hooks != NULL && s_hooks->drillHook != NULL ? s_hooks->drillHook(phase, pads) : 0u;
    }

    float extraOsdTopStartY(bool isTopRight)
    {
        return s_hooks != NULL && s_hooks->osdTopStartY != NULL ? s_hooks->osdTopStartY(isTopRight) : 0.0f;
    }
}
}
