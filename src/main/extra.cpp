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

    void extraCharacterRng()
    {
        if (s_hooks != NULL)
        {
            s_hooks->characterRng(kRngApply);
        }
    }

    bool extraPeachRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngPeach);
    }

    bool extraGameWatchRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngGameWatch);
    }

    bool extraLuigiRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngLuigi);
    }

    bool extraSamusRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngSamus);
    }

    bool extraMewtwoRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngMewtwo);
    }

    bool extraLucarioRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngLucario);
    }

    bool extraWarioRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngWario);
    }

    bool extraDonkeyKongRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngDonkeyKong);
    }

    bool extraIvysaurRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngIvysaur);
    }

    bool extraRobRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngRob);
    }

    bool extraLucasRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngLucas);
    }

    bool extraSquirtleRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngSquirtle);
    }

    bool extraSheikRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngSheik);
    }

    bool extraDededeRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngDedede);
    }

    bool extraSnakeRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngSnake);
    }

    bool extraGanondorfRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngGanondorf);
    }

    bool extraIceClimbersRows()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngIceClimbers);
    }

    bool extraCharacterRngPage()
    {
        return s_hooks != NULL && s_hooks->characterRng(kRngAny);
    }

    void extraStageOptionsTick()
    {
        if (s_hooks != NULL && s_hooks->stageOptions != NULL)
        {
            s_hooks->stageOptions(kStageTick);
        }
    }

    void extraStageOptionsChanged()
    {
        if (s_hooks != NULL && s_hooks->stageOptions != NULL)
        {
            s_hooks->stageOptions(kStageChanged);
        }
    }

    void extraStageOptionsSavestateLoaded()
    {
        if (s_hooks != NULL && s_hooks->stageOptions != NULL)
        {
            s_hooks->stageOptions(kStageSavestateLoaded);
        }
    }

    bool extraStageOptionsPage()
    {
        return s_hooks != NULL && s_hooks->stageOptions != NULL && s_hooks->stageOptions(kStagePage);
    }

    bool extraStageHazardsRow()
    {
        return s_hooks != NULL && s_hooks->stageOptions != NULL && s_hooks->stageOptions(kStageHazardsRow);
    }

    bool extraStagePlatformsRow()
    {
        return s_hooks != NULL && s_hooks->stageOptions != NULL && s_hooks->stageOptions(kStagePlatformsRow);
    }

    bool extraStageFodRow()
    {
        return s_hooks != NULL && s_hooks->stageOptions != NULL && s_hooks->stageOptions(kStageFodRow);
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
        if (s_hooks != NULL && s_hooks->scenarioCall != NULL)
        {
            s_hooks->scenarioCall(kScenarioChanged);
        }
    }

    void extraScenarioResetNow()
    {
        if (s_hooks != NULL && s_hooks->scenarioCall != NULL)
        {
            s_hooks->scenarioCall(kScenarioResetNow);
        }
    }

    void extraScenarioSavestateLoaded()
    {
        if (s_hooks != NULL && s_hooks->scenarioCall != NULL)
        {
            s_hooks->scenarioCall(kScenarioSavestateLoaded);
        }
    }

    void extraScenarioSettingsReset()
    {
        if (s_hooks != NULL && s_hooks->scenarioCall != NULL)
        {
            s_hooks->scenarioCall(kScenarioSettingsReset);
        }
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
}
}
