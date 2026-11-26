// Host tests for the pure Tech Practice HUD & optional 10/20-Rep Set state
// machine (src/extra/drill_hud_core.h) and stable DrillId session layout
// (src/shared/drill_session.h).
#include <string.h>

#include "extra/drill_hud_core.h"
#include "host/test.h"
#include "main/ui/hud_layout.h"
#include "shared/drill_session.h"

using namespace tm;
using namespace tm::extra;

static DrillEngineFeed emptyFeed()
{
    DrillEngineFeed f;
    memset(&f, 0, sizeof(f));
    return f;
}

TEST(drill_hud, session_ids_and_layout_stable)
{
    CHECK_EQ(0, static_cast<int>(drills::kDrillNone));
    CHECK_EQ(1, static_cast<int>(drills::kDrillLedgedash));
    CHECK_EQ(2, static_cast<int>(drills::kDrillWavedash));
    CHECK_EQ(3, static_cast<int>(drills::kDrillLCancel));
    CHECK_EQ(4, static_cast<int>(drills::kDrillHoverFloat));
    CHECK_EQ(5, static_cast<int>(drills::kDrillPowershield));
    CHECK_EQ(6, static_cast<int>(drills::kDrillRar));
    CHECK_EQ(7, static_cast<int>(drills::kDrillDacus));
    CHECK_EQ(8, static_cast<int>(drills::kDrillBReverse));
    CHECK_EQ(9, static_cast<int>(drills::kDrillCancelSprint));
    CHECK_EQ(10, static_cast<int>(drills::kDrillFlowCourse));
    CHECK_EQ(11, static_cast<int>(drills::kDrillReactionLab));
    CHECK_EQ(12, static_cast<int>(drills::kDrillTechchaseEscape));
    CHECK_EQ(13, static_cast<int>(drills::kDrillRecoverySweetspot));
    CHECK_EQ(14, static_cast<int>(drills::kDrillFloorhug));
    CHECK_EQ(15, static_cast<int>(drills::kDrillSnakeRecovery));
    CHECK_EQ(17, static_cast<int>(drills::kDrillComboBreakRush));
    CHECK_EQ(18, static_cast<int>(drills::kDrillIdCount));
    CHECK(static_cast<int>(drills::kDrillIdCount) <= static_cast<int>(drills::kBestSlots));
    CHECK_EQ(88u, static_cast<unsigned>(sizeof(drills::Session)));
    CHECK_EQ(5, static_cast<int>(drills::kSessionVersion));

    CHECK_EQ(6u, static_cast<unsigned>(drills::betterBest(drills::kDrillLedgedash, 0, 6)));
    CHECK_EQ(8u, static_cast<unsigned>(drills::betterBest(drills::kDrillLedgedash, 6, 8)));
    CHECK_EQ(8u, static_cast<unsigned>(drills::betterBest(drills::kDrillLedgedash, 8, 5)));
}

TEST(drill_hud, free_practice_default_runs_infinite_reps_and_persists_best_streak)
{
    DrillHudState hud;
    drillHudInit(hud, drills::kDrillLedgedash, 5);
    CHECK_EQ(static_cast<int>(kDrillModeFree), static_cast<int>(hud.mode));
    CHECK_EQ(0, static_cast<int>(drillModeTargetReps(hud.mode)));
    CHECK(strcmp(drillModeLabel(hud.mode), "FREE") == 0);

    LedgeCoreState ledge;
    ledgeCoreResetAll(ledge);
    DrillEngineFeed f = emptyFeed();
    f.ledge = &ledge;
    drillHudSyncBaselines(hud, f);

    // 15 consecutive hits in Free Practice must NEVER open the modal and must
    // track totalHit/totalAtt, best streak, and sessionBest past 10 reps.
    for (int i = 0; i < 15; i++)
    {
        ledge.seq++;
        ledge.last.outcome = kLedgeOutcomeLedgedash;
        ledge.last.galint = static_cast<s16>(6 + (i % 5));
        ledge.last.noXlu = 0;
        ledge.last.dropFrame = 1;
        CHECK(drillHudStep(hud, f));
        CHECK_EQ(0, static_cast<int>(hud.modalOpen));
    }
    CHECK_EQ(15u, static_cast<unsigned>(hud.totalAtt));
    CHECK_EQ(15u, static_cast<unsigned>(hud.totalHit));
    CHECK_EQ(15u, static_cast<unsigned>(hud.streakFull));
    CHECK_EQ(15u, static_cast<unsigned>(hud.bestStreakFull));
    CHECK_EQ(15u, static_cast<unsigned>(hud.sessionBest));
    CHECK_EQ(1, static_cast<int>(hud.newBest));
    CHECK_EQ(10, static_cast<int>(hud.freePipCount));

    // A miss resets streak but keeps sessionBest and still never opens modal.
    ledge.seq++;
    ledge.last.outcome = kLedgeOutcomeSd;
    CHECK(drillHudStep(hud, f));
    CHECK_EQ(0, static_cast<int>(hud.modalOpen));
    CHECK_EQ(16u, static_cast<unsigned>(hud.totalAtt));
    CHECK_EQ(15u, static_cast<unsigned>(hud.totalHit));
    CHECK_EQ(0u, static_cast<unsigned>(hud.streakFull));
    CHECK_EQ(15u, static_cast<unsigned>(hud.sessionBest));
    CHECK_EQ(static_cast<int>(kRepPipMiss), static_cast<int>(hud.repPips[kDrillFreePips - 1]));

    // Mode cycling: Free -> 10-Rep -> 20-Rep -> Free, preserving sessionBest.
    drillHudCycleMode(hud);
    CHECK_EQ(static_cast<int>(kDrillModeSet10), static_cast<int>(hud.mode));
    CHECK_EQ(15u, static_cast<unsigned>(hud.sessionBest));
    CHECK_EQ(0u, static_cast<unsigned>(hud.totalAtt));

    drillHudCycleMode(hud);
    CHECK_EQ(static_cast<int>(kDrillModeSet20), static_cast<int>(hud.mode));
    CHECK_EQ(20, static_cast<int>(drillModeTargetReps(hud.mode)));

    // 20-Rep Set runs cleanly to 20 reps and opens modal on rep 20 only.
    drillHudSyncBaselines(hud, f);
    for (int i = 0; i < 19; i++)
    {
        ledge.seq++;
        ledge.last.outcome = kLedgeOutcomeLedgedash;
        ledge.last.galint = 8;
        ledge.last.noXlu = 0;
        ledge.last.dropFrame = 1;
        CHECK(drillHudStep(hud, f));
        CHECK_EQ(0, static_cast<int>(hud.modalOpen));
    }
    ledge.seq++;
    ledge.last.outcome = kLedgeOutcomeLedgedash;
    ledge.last.galint = 9;
    CHECK(drillHudStep(hud, f));
    CHECK_EQ(20, static_cast<int>(hud.repsDone));
    CHECK_EQ(20u, static_cast<unsigned>(hud.totalHit));
    CHECK_EQ(1, static_cast<int>(hud.modalOpen));
    CHECK_EQ(20u, static_cast<unsigned>(hud.sessionBest));

    drillHudCycleMode(hud);
    CHECK_EQ(static_cast<int>(kDrillModeFree), static_cast<int>(hud.mode));
    CHECK_EQ(0, static_cast<int>(hud.modalOpen));
}

TEST(drill_hud, ledgedash_10_rep_set_opens_modal_and_tracks_best)
{
    DrillHudState hud;
    drillHudInitMode(hud, drills::kDrillLedgedash, 5, kDrillModeSet10);
    LedgeCoreState ledge;
    ledgeCoreResetAll(ledge);
    DrillEngineFeed f = emptyFeed();
    f.ledge = &ledge;
    drillHudSyncBaselines(hud, f);

    CHECK_EQ(0, static_cast<int>(hud.repsDone));
    CHECK_EQ(0, static_cast<int>(hud.modalOpen));

    // Interrupted ledge event is ignored (does not consume a rep).
    ledge.seq++;
    ledge.last.outcome = kLedgeOutcomeInterrupted;
    CHECK(!drillHudStep(hud, f));
    CHECK_EQ(0, static_cast<int>(hud.repsDone));

    // 7 hits (peak GALINT +11f), 1 grab-6 no-invuln, 1 tourney winner, 1 SD = 10 reps.
    for (int i = 0; i < 7; i++)
    {
        ledge.seq++;
        ledge.last.outcome = kLedgeOutcomeLedgedash;
        ledge.last.galint = static_cast<s16>(5 + i);
        ledge.last.noXlu = 0;
        ledge.last.dropFrame = 1;
        CHECK(drillHudStep(hud, f));
    }
    CHECK_EQ(7, static_cast<int>(hud.repsDone));
    CHECK_EQ(7, static_cast<int>(hud.repsHit));
    CHECK_EQ(7, static_cast<int>(hud.bestStreak));
    CHECK_EQ(11, static_cast<int>(hud.bestMetric));
    CHECK_EQ(1, static_cast<int>(hud.newBest));
    CHECK_EQ(7, static_cast<int>(hud.sessionBest));
    CHECK(strcmp(hud.lastFeedback, "GALINT +11f INVULN") == 0);

    ledge.seq++;
    ledge.last.outcome = kLedgeOutcomeNoInvulnLimit;
    ledge.last.grabCount = 6;
    CHECK(drillHudStep(hud, f));
    CHECK_EQ(static_cast<int>(kDrillGradeWarn), static_cast<int>(hud.lastGrade));
    CHECK(strcmp(hud.lastFeedback, "GRAB 6/5 (NO INVULN)") == 0);
    CHECK_EQ(0, static_cast<int>(hud.streak));

    ledge.seq++;
    ledge.last.outcome = kLedgeOutcomeTourneyWinner;
    CHECK(drillHudStep(hud, f));
    CHECK(strcmp(hud.lastFeedback, "TOURNEY WINNER") == 0);
    CHECK_EQ(0, static_cast<int>(hud.modalOpen));

    ledge.seq++;
    ledge.last.outcome = kLedgeOutcomeSd;
    CHECK(drillHudStep(hud, f));
    CHECK(strcmp(hud.lastFeedback, "LEDGESLIP / SD") == 0);
    CHECK_EQ(10, static_cast<int>(hud.repsDone));
    CHECK_EQ(7, static_cast<int>(hud.repsHit));
    CHECK_EQ(1, static_cast<int>(hud.modalOpen));

    char hl[kDrillHighlightBuf];
    drillHudFormatHighlight(hud, hl, sizeof(hl));
    CHECK(strcmp(hl, "+11f") == 0);

    // Retry resets reps & closes modal while preserving mode, sessionBest and incrementing retryCount.
    drillHudRetry(hud);
    CHECK_EQ(static_cast<int>(kDrillModeSet10), static_cast<int>(hud.mode));
    CHECK_EQ(0, static_cast<int>(hud.repsDone));
    CHECK_EQ(0, static_cast<int>(hud.repsHit));
    CHECK_EQ(0, static_cast<int>(hud.modalOpen));
    CHECK_EQ(1, static_cast<int>(hud.retryCount));
    CHECK_EQ(7, static_cast<int>(hud.sessionBest));
    CHECK(strcmp(hud.lastFeedback, "READY") == 0);

    // Remaining Ledgedash miss outcomes must map to their exact coaching labels.
    drillHudSyncBaselines(hud, f);
    ledge.seq++;
    ledge.last.outcome = kLedgeOutcomeOtherGetup;
    CHECK(drillHudStep(hud, f));
    CHECK(strcmp(hud.lastFeedback, "LEDGE GETUP") == 0);

    ledge.seq++;
    ledge.last.outcome = kLedgeOutcomeLandedNoDodge;
    CHECK(drillHudStep(hud, f));
    CHECK(strcmp(hud.lastFeedback, "NO AIRDODGE") == 0);
}

TEST(drill_hud, wavedash_and_lcancel_grading_and_late_upgrade)
{
    // Wavedash + Waveland (including angle readout).
    DrillHudState wdHud;
    drillHudInit(wdHud, drills::kDrillWavedash, 0);
    WdCoreState wd;
    wdCoreResetAll(wd);
    DrillEngineFeed fw = emptyFeed();
    fw.wd = &wd;
    drillHudSyncBaselines(wdHud, fw);

    wd.seq++;
    wd.last.kind = kWdKindWavedash;
    wd.last.outcome = kWdOutcomeSuccess;
    wd.last.jumpsquatToDodge = 1;
    wd.last.angleDeg = 17.5f;
    CHECK(drillHudStep(wdHud, fw));
    CHECK(strcmp(wdHud.lastFeedback, "PERFECT f1 17.5") == 0);
    CHECK_EQ(1, static_cast<int>(wdHud.bestMetric));

    wd.seq++;
    wd.last.kind = kWdKindWaveland;
    wd.last.outcome = kWdOutcomeSuccess;
    wd.last.airdodgeAirFrames = 2;
    CHECK(drillHudStep(wdHud, fw));
    CHECK(strcmp(wdHud.lastFeedback, "WAVELAND f2") == 0);
    CHECK_EQ(2, static_cast<int>(wdHud.repsHit));

    wd.seq++;
    wd.last.kind = kWdKindWavedash;
    wd.last.outcome = kWdMissLateAirdodge;
    CHECK(drillHudStep(wdHud, fw));
    CHECK(strcmp(wdHud.lastFeedback, "LATE AIRDODGE") == 0);
    CHECK_EQ(3, static_cast<int>(wdHud.repsDone));
    CHECK_EQ(2, static_cast<int>(wdHud.repsHit));

    // L-Cancel: manual hit, autocancel (non-rep), miss->late upgrade (no double count), ALC warning.
    DrillHudState lcHud;
    drillHudInit(lcHud, drills::kDrillLCancel, 0);
    LcCoreState lc;
    lcCoreResetAll(lc);
    DrillEngineFeed fl = emptyFeed();
    fl.lc = &lc;
    drillHudSyncBaselines(lcHud, fl);

    lc.seq++;
    lc.stats.total++;
    lc.last.outcome = kLcSuccessManual;
    lc.last.pressFrame = 3;
    CHECK(drillHudStep(lcHud, fl));
    CHECK(strcmp(lcHud.lastFeedback, "L-CANCEL f3/7") == 0);
    CHECK_EQ(1, static_cast<int>(lcHud.repsDone));
    CHECK_EQ(1, static_cast<int>(lcHud.repsHit));
    CHECK_EQ(3, static_cast<int>(lcHud.bestMetric));

    // Autocanceled aerial updates feedback without incrementing repsDone.
    lc.seq++;
    lc.last.outcome = kLcAutoCanceled;
    CHECK(drillHudStep(lcHud, fl));
    CHECK(strcmp(lcHud.lastFeedback, "AUTOCANCELED") == 0);
    CHECK_EQ(1, static_cast<int>(lcHud.repsDone));

    // Missed L-cancel (total++), then upgraded to kLcMissLate (total unchanged -> must NOT add rep).
    lc.seq++;
    lc.stats.total++;
    lc.last.outcome = kLcMissNoPress;
    CHECK(drillHudStep(lcHud, fl));
    CHECK_EQ(2, static_cast<int>(lcHud.repsDone));
    CHECK(strcmp(lcHud.lastFeedback, "MISSED L-CANCEL") == 0);

    lc.seq++;
    lc.last.outcome = kLcMissLate;
    lc.last.lateFrames = 4;
    CHECK(drillHudStep(lcHud, fl));
    CHECK_EQ(2, static_cast<int>(lcHud.repsDone));
    CHECK(strcmp(lcHud.lastFeedback, "LATE +4f") == 0);

    // Auto L-Cancel rule active -> flagged as warning, not a manual hit.
    lc.seq++;
    lc.stats.total++;
    lc.last.outcome = kLcSuccessAuto;
    lc.last.autoLCancelActive = 1;
    fl.autoLCancel = true;
    CHECK(drillHudStep(lcHud, fl));
    CHECK_EQ(3, static_cast<int>(lcHud.repsDone));
    CHECK_EQ(1, static_cast<int>(lcHud.repsHit));
    CHECK_EQ(1, static_cast<int>(lcHud.alcNotice));
    CHECK(strcmp(lcHud.lastFeedback, "AUTO L-CANCEL ON") == 0);
}

TEST(drill_hud, hover_ps_and_split_pplus_tech_drills)
{
    // Drill 4: Float / Hover & DJC ineligible fighter warning before any rep completes.
    DrillHudState fdHud;
    drillHudInit(fdHud, drills::kDrillHoverFloat, 0);
    FloatDjcSlot fd;
    floatDjcResetAll(fd);
    DrillEngineFeed ff = emptyFeed();
    ff.fd = &fd;
    drillHudSyncBaselines(fdHud, ff);

    ff.ineligibleFighter = true;
    CHECK(!drillHudStep(fdHud, ff));
    CHECK_EQ(static_cast<int>(kDrillGradeWarn), static_cast<int>(fdHud.lastGrade));
    CHECK_EQ(1, static_cast<int>(fdHud.alcNotice));
    CHECK(strcmp(fdHud.lastFeedback, "PICK PEACH/MEWTWO/DJC") == 0);
    ff.ineligibleFighter = false;

    fd.stats.fcAttempts++;
    fd.last.fcOutcome = kFcSuccess;
    fd.last.fcIsHover = 0;
    CHECK(drillHudStep(fdHud, ff));
    CHECK(strcmp(fdHud.lastFeedback, "FLOAT CANCEL") == 0);

    fd.stats.fcAttempts++;
    fd.last.fcOutcome = kFcSuccess;
    fd.last.fcIsHover = 1;
    CHECK(drillHudStep(fdHud, ff));
    CHECK(strcmp(fdHud.lastFeedback, "HOVER CANCEL") == 0);

    fd.stats.djcAttempts++;
    fd.last.djcGrade = kDjcInstant;
    fd.last.djcFrame = 1;
    CHECK(drillHudStep(fdHud, ff));
    CHECK(strcmp(fdHud.lastFeedback, "INSTANT DJC f1") == 0);
    CHECK_EQ(3, static_cast<int>(fdHud.repsHit));

    // Drill 5: Powershield & PS-Drop.
    DrillHudState psHud;
    drillHudInit(psHud, drills::kDrillPowershield, 0);
    PsSlot ps;
    psResetAll(ps);
    DrillEngineFeed fp = emptyFeed();
    fp.ps = &ps;
    drillHudSyncBaselines(psHud, fp);

    ps.stats.attempts++;
    ps.last.outcome = kPsPhysical;
    ps.last.windowFrame = 1;
    CHECK(drillHudStep(psHud, fp));
    CHECK(strcmp(psHud.lastFeedback, "POWERSHIELD f1") == 0);
    CHECK_EQ(1, static_cast<int>(psHud.repsDone));
    CHECK_EQ(1, static_cast<int>(psHud.repsHit));

    // PS-Drop increments highlight metric without adding an extra rep.
    ps.stats.psDropCancels++;
    ps.last.psDropActFrame = 2;
    CHECK(drillHudStep(psHud, fp));
    CHECK(strcmp(psHud.lastFeedback, "PS-DROP f2") == 0);
    CHECK_EQ(1, static_cast<int>(psHud.repsDone));
    CHECK_EQ(1, static_cast<int>(psHud.bestMetric));

    // Drill 6: Dedicated RAR (ignores DACUS/B-Reverse events).
    RarState rar;
    DacusState dacus;
    BReverseState brev;
    rarReset(rar);
    dacusReset(dacus);
    breverseReset(brev);
    DrillEngineFeed ft = emptyFeed();
    ft.rar = &rar;
    ft.dacus = &dacus;
    ft.brev = &brev;

    DrillHudState rarHud;
    drillHudInit(rarHud, drills::kDrillRar, 0);
    drillHudSyncBaselines(rarHud, ft);
    dacus.attempts++;
    dacus.last.outcome = kDacusSuccessGatling;
    CHECK(!drillHudStep(rarHud, ft));
    rar.attempts++;
    rar.last.outcome = kRarSuccessBair;
    rar.last.turnRunFrames = 1;
    CHECK(drillHudStep(rarHud, ft));
    CHECK(strcmp(rarHud.lastFeedback, "RAR BAIR f1") == 0);
    char hl[kDrillHighlightBuf];
    drillHudFormatHighlight(rarHud, hl, sizeof(hl));
    CHECK(strcmp(hl, "Frame 1") == 0);

    // Drill 7: Dedicated DACUS (ignores RAR/B-Reverse events).
    DrillHudState dacusHud;
    drillHudInit(dacusHud, drills::kDrillDacus, 0);
    drillHudSyncBaselines(dacusHud, ft);
    rar.attempts++;
    CHECK(!drillHudStep(dacusHud, ft));
    dacus.attempts++;
    dacus.last.outcome = kDacusSuccessGatling;
    dacus.last.nonHitlagDashFrame = 2;
    CHECK(drillHudStep(dacusHud, ft));
    CHECK(strcmp(dacusHud.lastFeedback, "GATLING DACUS f2") == 0);
    drillHudFormatHighlight(dacusHud, hl, sizeof(hl));
    CHECK(strcmp(hl, "1") == 0);

    // Drill 8: Dedicated B-Reverse / Wavebounce (ignores RAR/DACUS events).
    DrillHudState brevHud;
    drillHudInit(brevHud, drills::kDrillBReverse, 0);
    drillHudSyncBaselines(brevHud, ft);
    dacus.attempts++;
    CHECK(!drillHudStep(brevHud, ft));
    brev.attempts++;
    brev.last.outcome = kBRevWavebounce;
    CHECK(drillHudStep(brevHud, ft));
    CHECK(strcmp(brevHud.lastFeedback, "WAVEBOUNCE") == 0);
    drillHudFormatHighlight(brevHud, hl, sizeof(hl));
    CHECK(strcmp(hl, "1") == 0);

    // Baseline sync after savestate reset ignores stale engine counter shifts.
    brev.attempts = 0;
    drillHudSyncBaselines(brevHud, ft);
    CHECK(!drillHudStep(brevHud, ft));
    CHECK_EQ(1, static_cast<int>(brevHud.repsDone));
}

TEST(drill_hud, geometry_within_crt_safe_area_43_and_169)
{
    for (int a = 0; a < 2; a++)
    {
        bool wide = (a == 1);
        ui::layout::Box safe = ui::layout::safeArea(wide);
        ui::layout::Box hud = ui::layout::drillHudBox(wide);
        ui::layout::Box modal = ui::layout::drillModalBox(wide);

        CHECK_NEAR(316.0f * ui::layout::xsOf(wide), hud.x1 - hud.x0, 0.01f);
        CHECK(hud.x0 >= safe.x0 && hud.x1 <= safe.x1);
        CHECK(hud.y0 >= safe.y0 && hud.y1 <= safe.y1);
        CHECK(hud.y1 < 397.0f);
        CHECK(modal.x0 >= safe.x0 && modal.x1 <= safe.x1);
        CHECK(modal.y0 >= safe.y0 && modal.y1 <= safe.y1);
    }
}

TEST(drill_hud, stage4c_all_seven_challenges_modes_timed60_and_bests)
{
    char hl[kDrillHighlightBuf];

    // 1. Cancel Sprint: ineligible fighter warning, live cancel hit, 60s Timed mode countdown & modal.
    {
        DrillHudState hud;
        drillHudInit(hud, drills::kDrillCancelSprint, 2);
        CHECK(isChallengeDrill(hud.activeDrill));
        CHECK_EQ(static_cast<int>(kDrillModeFree), static_cast<int>(hud.mode));

        CancelSprintState cs;
        cancelSprintResetAll(cs);
        DrillEngineFeed f = emptyFeed();
        f.cs = &cs;
        drillHudSyncBaselines(hud, f);

        f.ineligibleFighter = true;
        CHECK(!drillHudStep(hud, f));
        CHECK(strcmp(hud.lastFeedback, "PICK SPACIE/PK/M2/LUC") == 0);
        f.ineligibleFighter = false;

        // Cycle Free -> 10-Rep -> 20-Rep -> 60s Timed -> Free.
        drillHudCycleMode(hud);
        CHECK_EQ(static_cast<int>(kDrillModeSet10), static_cast<int>(hud.mode));
        drillHudCycleMode(hud);
        CHECK_EQ(static_cast<int>(kDrillModeSet20), static_cast<int>(hud.mode));
        drillHudCycleMode(hud);
        CHECK_EQ(static_cast<int>(kDrillModeTimed60), static_cast<int>(hud.mode));
        CHECK_EQ(static_cast<unsigned>(kDrillTimedFrames), static_cast<unsigned>(hud.timerFrames));
        CHECK(strcmp(drillModeLabel(hud.mode), "60s") == 0);
        drillHudSyncBaselines(hud, f);

        // Score 3 cancels in 60s Timed mode -> sessionBest grows from 2 to 3.
        for (int i = 0; i < 3; i++)
        {
            cs.out.attempts++;
            cs.out.totalCancels++;
            cs.out.lastSuccess = 1;
            cs.out.lastFrames = static_cast<u16>(i == 0 ? 2 : 1);
            CHECK(drillHudStep(hud, f));
        }
        CHECK_EQ(3u, static_cast<unsigned>(hud.totalHit));
        CHECK_EQ(3u, static_cast<unsigned>(hud.sessionBest));
        CHECK_EQ(1, static_cast<int>(hud.newBest));
        CHECK(strcmp(hud.lastFeedback, "PERFECT CANCEL f1") == 0);
        drillHudFormatHighlight(hud, hl, sizeof(hl));
        CHECK(strcmp(hl, "Frame 1") == 0);

        // A miss mid-run breaks streakFull, then a 4th hit still grows sessionBest
        // using totalHit (consistent with 60s expiry score in drillHudComplete).
        cs.out.attempts++;
        cs.out.lastSuccess = 0;
        CHECK(drillHudStep(hud, f));
        CHECK_EQ(0u, static_cast<unsigned>(hud.streakFull));
        cs.out.attempts++;
        cs.out.totalCancels++;
        cs.out.lastSuccess = 1;
        cs.out.lastFrames = 1;
        CHECK(drillHudStep(hud, f));
        CHECK_EQ(4u, static_cast<unsigned>(hud.totalHit));
        CHECK_EQ(1u, static_cast<unsigned>(hud.streakFull));
        CHECK_EQ(4u, static_cast<unsigned>(hud.sessionBest));

        // Countdown timer reaches 0 -> opens modal automatically.
        hud.timerFrames = 2;
        CHECK(!drillHudTickTimer(hud));
        CHECK_EQ(0, static_cast<int>(hud.modalOpen));
        CHECK(drillHudTickTimer(hud));
        CHECK_EQ(1, static_cast<int>(hud.modalOpen));
        CHECK_EQ(0u, static_cast<unsigned>(hud.timerFrames));

        // Retry resets timer back to 3600 frames while keeping sessionBest.
        drillHudRetry(hud);
        CHECK_EQ(0, static_cast<int>(hud.modalOpen));
        CHECK_EQ(static_cast<unsigned>(kDrillTimedFrames), static_cast<unsigned>(hud.timerFrames));
        CHECK_EQ(4u, static_cast<unsigned>(hud.sessionBest));

        drillHudCycleMode(hud);
        CHECK_EQ(static_cast<int>(kDrillModeFree), static_cast<int>(hud.mode));
    }

    // 2. Movement Flow Course: completed course updates streak and fastest time highlight.
    {
        DrillHudState hud;
        drillHudInit(hud, drills::kDrillFlowCourse, 0);
        FlowCourseState fc;
        flowCourseResetAll(fc);
        DrillEngineFeed f = emptyFeed();
        f.fc = &fc;
        drillHudSyncBaselines(hud, f);

        fc.out.coursesCompleted = 1;
        fc.out.currentElapsed = 148;
        fc.out.bestCourseFrames = 148;
        CHECK(drillHudStep(hud, f));
        CHECK_EQ(1u, static_cast<unsigned>(hud.totalHit));
        CHECK_EQ(148, static_cast<int>(hud.bestMetric));
        CHECK(strcmp(hud.lastFeedback, "COURSE 148f") == 0);
        drillHudFormatHighlight(hud, hl, sizeof(hl));
        CHECK(strcmp(hl, "148f") == 0);
    }

    // 3. Situational Reaction Lab: fast reaction counts as hit, slow (>26f) counts as warn/miss.
    {
        DrillHudState hud;
        drillHudInit(hud, drills::kDrillReactionLab, 0);
        u16 ring[kReactionRingCap];
        ReactionTechchaseState rt;
        reactionTechchaseResetAll(rt, ring, kReactionRingCap);
        DrillEngineFeed f = emptyFeed();
        f.rt = &rt;
        drillHudSyncBaselines(hud, f);

        rt.out.rlCount = 1;
        rt.out.rlLastFrames = 16;
        CHECK(drillHudStep(hud, f));
        CHECK_EQ(1u, static_cast<unsigned>(hud.totalHit));
        CHECK(strcmp(hud.lastFeedback, "REACT 16f") == 0);
        drillHudFormatHighlight(hud, hl, sizeof(hl));
        CHECK(strcmp(hl, "16f") == 0);

        rt.out.rlCount = 2;
        rt.out.rlLastFrames = 30;
        CHECK(drillHudStep(hud, f));
        CHECK_EQ(2u, static_cast<unsigned>(hud.totalAtt));
        CHECK_EQ(1u, static_cast<unsigned>(hud.totalHit));
        CHECK_EQ(static_cast<int>(kDrillGradeWarn), static_cast<int>(hud.lastGrade));
        CHECK(strcmp(hud.lastFeedback, "REACT 30f") == 0);
    }

    // 4. Universal Techchase Escape: escape vs caught.
    {
        DrillHudState hud;
        drillHudInit(hud, drills::kDrillTechchaseEscape, 0);
        u16 ring[kReactionRingCap];
        ReactionTechchaseState rt;
        reactionTechchaseResetAll(rt, ring, kReactionRingCap);
        DrillEngineFeed f = emptyFeed();
        f.rt = &rt;
        drillHudSyncBaselines(hud, f);

        rt.out.tcEscapes = 1;
        rt.out.tcLastTightness = 0;
        CHECK(drillHudStep(hud, f));
        CHECK(strcmp(hud.lastFeedback, "PERFECT ESC f0") == 0);
        CHECK_EQ(1u, static_cast<unsigned>(hud.totalHit));

        rt.out.tcCaughtCount = 1;
        CHECK(drillHudStep(hud, f));
        CHECK(strcmp(hud.lastFeedback, "CAUGHT") == 0);
        CHECK_EQ(2u, static_cast<unsigned>(hud.totalAtt));
        CHECK_EQ(0u, static_cast<unsigned>(hud.streakFull));
    }

    // 5. Universal Recovery Sweetspot: sweetspot, high overshoot, no-invuln limit, SD.
    {
        DrillHudState hud;
        drillHudInit(hud, drills::kDrillRecoverySweetspot, 0);
        RecoveryFloorhugState rf;
        recoveryFloorhugResetAll(rf);
        DrillEngineFeed f = emptyFeed();
        f.rf = &rf;
        drillHudSyncBaselines(hud, f);

        rf.out.recAttempts = 1;
        rf.out.recLastGrade = kRecGradeSweetspot;
        rf.out.recLastXlu = 31;
        CHECK(drillHudStep(hud, f));
        CHECK(strcmp(hud.lastFeedback, "SWEETSPOT +31f") == 0);
        CHECK_EQ(1u, static_cast<unsigned>(hud.totalHit));

        rf.out.recAttempts = 2;
        rf.out.recLastGrade = kRecGradeHighOvershoot;
        CHECK(drillHudStep(hud, f));
        CHECK(strcmp(hud.lastFeedback, "HIGH OVERSHOOT") == 0);
    }

    // 6. Floorhug / ASDI-Down Tech: success with hitstun saved vs miss.
    {
        DrillHudState hud;
        drillHudInit(hud, drills::kDrillFloorhug, 0);
        RecoveryFloorhugState rf;
        recoveryFloorhugResetAll(rf);
        DrillEngineFeed f = emptyFeed();
        f.rf = &rf;
        drillHudSyncBaselines(hud, f);

        rf.out.fhReps = 1;
        rf.out.fhSuccesses = 1;
        rf.lastFhOutcome = kFhCcFloorhugCancel;
        rf.lastHitstunSaved = 9;
        CHECK(drillHudStep(hud, f));
        CHECK(strcmp(hud.lastFeedback, "FLOORHUG -9f") == 0);
        CHECK_EQ(1u, static_cast<unsigned>(hud.totalHit));
        drillHudFormatHighlight(hud, hl, sizeof(hl));
        CHECK(strcmp(hl, "9f") == 0);

        rf.out.fhReps = 2;
        rf.lastFhOutcome = kFhMissedFloorhug;
        CHECK(drillHudStep(hud, f));
        CHECK(strcmp(hud.lastFeedback, "MISSED FLOORHUG") == 0);
    }

    // 7. Snake Infinite Recovery: ineligible warning, C4 refresh hit, recovery completed.
    {
        DrillHudState hud;
        drillHudInit(hud, drills::kDrillSnakeRecovery, 0);
        SnakeRecoveryState snk;
        snakeRecoveryResetAll(snk);
        DrillEngineFeed f = emptyFeed();
        f.snk = &snk;
        drillHudSyncBaselines(hud, f);

        f.ineligibleFighter = true;
        CHECK(!drillHudStep(hud, f));
        CHECK(strcmp(hud.lastFeedback, "PICK SNAKE") == 0);
        f.ineligibleFighter = false;

        snk.out.refreshCount = 1;
        snk.out.loopChain = 2;
        snk.out.bestLoopChain = 2;
        snk.out.lastSelfHitKind = kSnakeSelfHitC4;
        CHECK(drillHudStep(hud, f));
        CHECK(strcmp(hud.lastFeedback, "C4 REFRESH x2") == 0);
        CHECK_EQ(2, static_cast<int>(hud.bestMetric));
        drillHudFormatHighlight(hud, hl, sizeof(hl));
        CHECK(strcmp(hl, "2 loops") == 0);

        snk.out.completedRecoveries = 1;
        CHECK(drillHudStep(hud, f));
        CHECK(strcmp(hud.lastFeedback, "RECOVERED x2") == 0);
        CHECK_EQ(1u, static_cast<unsigned>(hud.totalHit));
    }
}
