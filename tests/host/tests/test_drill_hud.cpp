// Host tests for the pure 10-rep Skill Drill HUD & End-Screen state machine
// (src/extra/drill_hud_core.h) and stable DrillId session layout
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
    CHECK_EQ(6, static_cast<int>(drills::kDrillPPlusTech));
    CHECK_EQ(7, static_cast<int>(drills::kDrillIdCount));
    CHECK_EQ(88u, static_cast<unsigned>(sizeof(drills::Session)));
    CHECK_EQ(5, static_cast<int>(drills::kSessionVersion));

    CHECK_EQ(6u, static_cast<unsigned>(drills::betterBest(drills::kDrillLedgedash, 0, 6)));
    CHECK_EQ(8u, static_cast<unsigned>(drills::betterBest(drills::kDrillLedgedash, 6, 8)));
    CHECK_EQ(8u, static_cast<unsigned>(drills::betterBest(drills::kDrillLedgedash, 8, 5)));
}

TEST(drill_hud, ledgedash_10_rep_set_opens_modal_and_tracks_best)
{
    DrillHudState hud;
    drillHudInit(hud, drills::kDrillLedgedash, 5);
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
    CHECK(strcmp(hud.lastFeedback, "GALINT +11f") == 0);

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
    CHECK_EQ(10, static_cast<int>(hud.repsDone));
    CHECK_EQ(7, static_cast<int>(hud.repsHit));
    CHECK_EQ(1, static_cast<int>(hud.modalOpen));

    char hl[kDrillHighlightBuf];
    drillHudFormatHighlight(hud, hl, sizeof(hl));
    CHECK(strcmp(hl, "+11f") == 0);

    // Retry resets reps & closes modal while preserving sessionBest and incrementing retryCount.
    drillHudRetry(hud);
    CHECK_EQ(0, static_cast<int>(hud.repsDone));
    CHECK_EQ(0, static_cast<int>(hud.repsHit));
    CHECK_EQ(0, static_cast<int>(hud.modalOpen));
    CHECK_EQ(1, static_cast<int>(hud.retryCount));
    CHECK_EQ(7, static_cast<int>(hud.sessionBest));
    CHECK(strcmp(hud.lastFeedback, "READY") == 0);
}

TEST(drill_hud, wavedash_and_lcancel_grading_and_late_upgrade)
{
    // Wavedash + Waveland.
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
    CHECK(drillHudStep(wdHud, fw));
    CHECK(strcmp(wdHud.lastFeedback, "PERFECT f1") == 0);
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

TEST(drill_hud, hover_ps_and_pplus_tech_drills)
{
    // Drill 4: Float / Hover & DJC.
    DrillHudState fdHud;
    drillHudInit(fdHud, drills::kDrillHoverFloat, 0);
    FloatDjcSlot fd;
    floatDjcResetAll(fd);
    DrillEngineFeed ff = emptyFeed();
    ff.fd = &fd;
    drillHudSyncBaselines(fdHud, ff);

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

    // Drill 6: P+ Tech (RAR, DACUS, B-Reverse / Wavebounce).
    DrillHudState tpHud;
    drillHudInit(tpHud, drills::kDrillPPlusTech, 0);
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
    drillHudSyncBaselines(tpHud, ft);

    rar.attempts++;
    rar.last.outcome = kRarSuccessBair;
    rar.last.turnRunFrames = 1;
    CHECK(drillHudStep(tpHud, ft));
    CHECK(strcmp(tpHud.lastFeedback, "RAR BAIR") == 0);

    dacus.attempts++;
    dacus.last.outcome = kDacusSuccessGatling;
    CHECK(drillHudStep(tpHud, ft));
    CHECK(strcmp(tpHud.lastFeedback, "GATLING DACUS") == 0);

    brev.attempts++;
    brev.last.outcome = kBRevWavebounce;
    CHECK(drillHudStep(tpHud, ft));
    CHECK(strcmp(tpHud.lastFeedback, "WAVEBOUNCE") == 0);

    CHECK_EQ(3, static_cast<int>(tpHud.repsHit));
    char hl[kDrillHighlightBuf];
    drillHudFormatHighlight(tpHud, hl, sizeof(hl));
    CHECK(strcmp(hl, "1 / 1 / 1") == 0);

    // Baseline sync after savestate reset ignores stale engine counter shifts.
    rar.attempts = 0;
    dacus.attempts = 0;
    brev.attempts = 0;
    drillHudSyncBaselines(tpHud, ft);
    CHECK(!drillHudStep(tpHud, ft));
    CHECK_EQ(3, static_cast<int>(tpHud.repsDone));
}

TEST(drill_hud, geometry_within_crt_safe_area_43_and_169)
{
    for (int a = 0; a < 2; a++)
    {
        bool wide = (a == 1);
        ui::layout::Box safe = ui::layout::safeArea(wide);
        ui::layout::Box hud = ui::layout::drillHudBox(wide);
        ui::layout::Box modal = ui::layout::drillModalBox(wide);

        CHECK(hud.x0 >= safe.x0 && hud.x1 <= safe.x1);
        CHECK(hud.y0 >= safe.y0 && hud.y1 <= safe.y1);
        CHECK(hud.y1 < 397.0f);
        CHECK(modal.x0 >= safe.x0 && modal.x1 <= safe.x1);
        CHECK(modal.y0 >= safe.y0 && modal.y1 <= safe.y1);
    }
}
