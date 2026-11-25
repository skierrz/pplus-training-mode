#include "main/extra.h"

#include "shared/log.h"
#include "shared/perf.h"

namespace tm {
namespace main {
    static const ExtraHooks* s_hooks = NULL;

    // Only the extra module calls this: kept from the linker's stripping,
    // as anything else only the extra module uses must be.
#pragma force_active on
    void setExtraHooks(const ExtraHooks* hooks)
    {
        s_hooks = hooks;
        TM_LOG("main: extra module %s\n", hooks != NULL ? "registered" : "gone");
    }
#pragma force_active reset

    void extraRender(u32 renderCall, const game::MatchState& match)
    {
        if (s_hooks != NULL && s_hooks->render != NULL)
        {
            s_hooks->render(renderCall, match);
        }
    }

    bool extraAlterRow()
    {
        return s_hooks != NULL && s_hooks->alterRow();
    }

    void extraAlterEdited()
    {
        if (s_hooks != NULL)
        {
            s_hooks->alterEdited();
        }
    }

    bool extraLoaded()
    {
        return s_hooks != NULL;
    }

#pragma dont_inline on
    static bool rngCall(int call)
    {
        return s_hooks != NULL && s_hooks->characterRng(call);
    }

    static bool stageCall(int call)
    {
        return s_hooks != NULL && s_hooks->stageOptions != NULL && s_hooks->stageOptions(call);
    }

    static void scenCall(int call)
    {
        if (s_hooks != NULL && s_hooks->scenarioCall != NULL)
        {
            s_hooks->scenarioCall(call);
        }
    }
#pragma dont_inline reset

    void extraCharacterRng()
    {
        rngCall(kRngApply);
    }

    bool extraPeachRows()
    {
        return rngCall(kRngPeach);
    }

    bool extraGameWatchRows()
    {
        return rngCall(kRngGameWatch);
    }

    bool extraLuigiRows()
    {
        return rngCall(kRngLuigi);
    }

    bool extraSamusRows()
    {
        return rngCall(kRngSamus);
    }

    bool extraMewtwoRows()
    {
        return rngCall(kRngMewtwo);
    }

    bool extraLucarioRows()
    {
        return rngCall(kRngLucario);
    }

    bool extraWarioRows()
    {
        return rngCall(kRngWario);
    }

    bool extraDonkeyKongRows()
    {
        return rngCall(kRngDonkeyKong);
    }

    bool extraIvysaurRows()
    {
        return rngCall(kRngIvysaur);
    }

    bool extraRobRows()
    {
        return rngCall(kRngRob);
    }

    bool extraLucasRows()
    {
        return rngCall(kRngLucas);
    }

    bool extraSquirtleRows()
    {
        return rngCall(kRngSquirtle);
    }

    bool extraSheikRows()
    {
        return rngCall(kRngSheik);
    }

    bool extraDededeRows()
    {
        return rngCall(kRngDedede);
    }

    bool extraSnakeRows()
    {
        return rngCall(kRngSnake);
    }

    bool extraGanondorfRows()
    {
        return rngCall(kRngGanondorf);
    }

    bool extraIceClimbersRows()
    {
        return rngCall(kRngIceClimbers);
    }

    bool extraOlimarRows()
    {
        return rngCall(kRngOlimar);
    }

    bool extraCharacterRngPage()
    {
        return rngCall(kRngAny);
    }

    void extraStageOptionsTick()
    {
        stageCall(kStageTick);
    }

    void extraStageOptionsChanged()
    {
        stageCall(kStageChanged);
    }

    void extraStageOptionsSavestateLoaded()
    {
        stageCall(kStageSavestateLoaded);
    }

    bool extraStageOptionsPage()
    {
        return stageCall(kStagePage);
    }

    bool extraStageHazardsRow()
    {
        return stageCall(kStageHazardsRow);
    }

    bool extraStagePlatformsRow()
    {
        return stageCall(kStagePlatformsRow);
    }

    bool extraStageFodRow()
    {
        return stageCall(kStageFodRow);
    }

    bool extraDiRow()
    {
        return s_hooks != NULL;
    }

    void extraDiDraw(const game::MatchState& match)
    {
        TM_PERF_SCOPE(kDiDraw);
        if (s_hooks != NULL)
        {
            s_hooks->diDraw(match);
        }
    }

    void extraDiReset()
    {
        if (s_hooks != NULL)
        {
            s_hooks->diReset();
        }
    }

    void extraDiHideDrawDi(bool hide)
    {
        if (s_hooks != NULL)
        {
            s_hooks->diHideDrawDi(hide);
        }
    }

    void extraHeatmapDraw(const game::MatchState& match)
    {
        if (s_hooks != NULL && s_hooks->heatmapDraw != NULL)
        {
            s_hooks->heatmapDraw(match);
        }
    }

    void extraHeatmapReset()
    {
        if (s_hooks != NULL && s_hooks->heatmapReset != NULL)
        {
            s_hooks->heatmapReset();
        }
    }

    void extraGrabVizDraw(const game::MatchState& match)
    {
        if (s_hooks != NULL && s_hooks->grabVizDraw != NULL)
        {
            s_hooks->grabVizDraw(match);
        }
    }

    int extraActIn(Fighter* fighter, int* how)
    {
        *how = kActNone;
        return s_hooks != NULL ? s_hooks->actIn(fighter, how) : -1;
    }

    void extraFrameData()
    {
        if (s_hooks != NULL)
        {
            s_hooks->frameData();
        }
    }

    bool extraAdvantageMeasured(const game::Advantage& r)
    {
        return s_hooks != NULL && s_hooks->advantageMeasured(r);
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
        if (s_hooks != NULL && s_hooks->pplusRecoveryReset != NULL)
        {
            s_hooks->pplusRecoveryReset(slot);
        }
    }

    void extraComboDraw(const game::MatchState& match)
    {
        if (s_hooks != NULL && s_hooks->comboDraw != NULL)
        {
            s_hooks->comboDraw(match);
        }
    }

    void extraComboReset()
    {
        if (s_hooks != NULL && s_hooks->comboReset != NULL)
        {
            s_hooks->comboReset();
        }
    }

    int extraAdaptiveBias(int cpuPlayerNo, int list, bool inCounter, float opponentDist, bool commit)
    {
        if (s_hooks != NULL && s_hooks->adaptiveBias != NULL)
        {
            return s_hooks->adaptiveBias(cpuPlayerNo, list, inCounter, opponentDist, commit);
        }
        return -1;
    }

    void extraAdaptiveReset()
    {
        if (s_hooks != NULL && s_hooks->adaptiveReset != NULL)
        {
            s_hooks->adaptiveReset();
        }
    }

    void extraScenarioChanged()
    {
        scenCall(kScenarioChanged);
    }

    void extraScenarioResetNow()
    {
        scenCall(kScenarioResetNow);
    }

    void extraScenarioSavestateLoaded()
    {
        scenCall(kScenarioSavestateLoaded);
    }

    void extraScenarioSettingsReset()
    {
        scenCall(kScenarioSettingsReset);
    }

    void extraScenarioCyclePlatform()
    {
        scenCall(kScenarioCyclePlatform);
    }

    void extraComboReposition()
    {
        scenCall(kScenarioComboReposition);
    }

    bool extraScenarioCpuStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out, u32 lastButtons)
    {
        return s_hooks != NULL && s_hooks->scenarioCpuStep != NULL
               && s_hooks->scenarioCpuStep(who, v, f, out, lastButtons);
    }

    void extraScenarioSavedView(Settings& out)
    {
        if (s_hooks != NULL && s_hooks->scenarioSavedView != NULL)
        {
            s_hooks->scenarioSavedView(out);
        }
    }

    bool extraFighterSwapRow()
    {
        return s_hooks != NULL && s_hooks->fighterSwapRow != NULL && s_hooks->fighterSwapRow();
    }

    void extraFighterSwapChanged()
    {
        if (s_hooks != NULL && s_hooks->fighterSwapChanged != NULL)
        {
            s_hooks->fighterSwapChanged();
        }
    }

    void extraFighterSwapApply()
    {
        if (s_hooks != NULL && s_hooks->fighterSwapApply != NULL)
        {
            s_hooks->fighterSwapApply();
        }
    }

    bool extraFighterSwapBusy()
    {
        return s_hooks != NULL && s_hooks->fighterSwapBusy != NULL && s_hooks->fighterSwapBusy();
    }

    const char* extraOptionDesc(u8 pageId, u8 row)
    {
        return (s_hooks != NULL && s_hooks->optionDesc != NULL) ? s_hooks->optionDesc(pageId, row) : NULL;
    }

    bool extraDrawUiIcon(u8 iconId, float x, float y, float w, float h, u32 rgba)
    {
        return s_hooks != NULL && s_hooks->drawUiIcon != NULL && s_hooks->drawUiIcon(iconId, x, y, w, h, rgba);
    }

    void extraDrawPageOverlay(u8 pageId, bool railMode)
    {
        if (s_hooks != NULL && s_hooks->drawPageOverlay != NULL)
        {
            s_hooks->drawPageOverlay(pageId, railMode);
        }
    }

    bool extraInfoWanted()
    {
        return s_hooks != NULL && s_hooks->infoWanted != NULL && s_hooks->infoWanted();
    }

    void extraInfoStep(bool previousRan)
    {
        if (s_hooks != NULL && s_hooks->infoStep != NULL)
        {
            s_hooks->infoStep(previousRan);
        }
    }

    void extraInfoDraw()
    {
        if (s_hooks != NULL && s_hooks->infoDraw != NULL)
        {
            s_hooks->infoDraw();
        }
    }

    void extraInfoReset()
    {
        if (s_hooks != NULL && s_hooks->infoReset != NULL)
        {
            s_hooks->infoReset();
        }
    }

    void extraInfoApplyPreset(int side)
    {
        if (s_hooks != NULL && s_hooks->infoApplyPreset != NULL)
        {
            s_hooks->infoApplyPreset(side);
        }
    }

    void extraInputViewerDraw(const game::MatchState& match, bool menuOpen)
    {
        if (s_hooks != NULL && s_hooks->inputViewerDraw != NULL)
        {
            s_hooks->inputViewerDraw(match, menuOpen);
        }
    }

    bool extraDrawColoredHitboxes(Fighter* fighter)
    {
        return s_hooks != NULL && s_hooks->drawColoredHitboxes != NULL && s_hooks->drawColoredHitboxes(fighter);
    }

    void extraDiCompassDraw(const game::MatchState& match, bool menuOpen)
    {
        if (s_hooks != NULL && s_hooks->diCompassDraw != NULL)
        {
            s_hooks->diCompassDraw(match, menuOpen);
        }
    }

    bool extraReversalPick(int trigger, int fighterKind, u32 roll, int& outAction, int& outDelay)
    {
        return s_hooks != NULL && s_hooks->reversalPick != NULL
               && s_hooks->reversalPick(trigger, fighterKind, roll, outAction, outDelay);
    }

    void extraPsaInspectorDraw(const game::MatchState& match, bool menuOpen)
    {
        if (s_hooks != NULL && s_hooks->psaInspectorDraw != NULL)
        {
            s_hooks->psaInspectorDraw(match, menuOpen);
        }
    }

    void extraTimingMeterDraw(const game::MatchState& match)
    {
        if (s_hooks != NULL && s_hooks->timingMeterDraw != NULL)
        {
            s_hooks->timingMeterDraw(match);
        }
    }

    u32 extraDrill(u32 phase, gfPadStatus* pads)
    {
        if (s_hooks != NULL && s_hooks->drillHook != NULL)
        {
            return s_hooks->drillHook(phase, pads);
        }
        return 0u;
    }
}
}
