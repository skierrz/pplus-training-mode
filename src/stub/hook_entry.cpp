#include "stub/hook_entry.h"

// Frame layout: 0x08 CR, 0x0C CTR, 0x10 XER, 0x18-0x87 f0-f13, 0x88 FPSCR,
// 0x94 LR (the caller's frame slot, as usual). r12 holds the body to call.
extern "C" asm void tmPreservingCall()
{
    nofralloc
    stwu r1, -0x90(r1)
    mflr r0
    stw r0, 0x94(r1)
    mfcr r0
    stw r0, 0x08(r1)
    mfctr r0
    stw r0, 0x0C(r1)
    mfxer r0
    stw r0, 0x10(r1)
    stfd f0, 0x18(r1)
    stfd f1, 0x20(r1)
    stfd f2, 0x28(r1)
    stfd f3, 0x30(r1)
    stfd f4, 0x38(r1)
    stfd f5, 0x40(r1)
    stfd f6, 0x48(r1)
    stfd f7, 0x50(r1)
    stfd f8, 0x58(r1)
    stfd f9, 0x60(r1)
    stfd f10, 0x68(r1)
    stfd f11, 0x70(r1)
    stfd f12, 0x78(r1)
    stfd f13, 0x80(r1)
    mffs f0
    stfd f0, 0x88(r1)

    mtctr r12
    bctrl

    lfd f0, 0x88(r1)
    mtfsf 0xFF, f0
    lfd f0, 0x18(r1)
    lfd f1, 0x20(r1)
    lfd f2, 0x28(r1)
    lfd f3, 0x30(r1)
    lfd f4, 0x38(r1)
    lfd f5, 0x40(r1)
    lfd f6, 0x48(r1)
    lfd f7, 0x50(r1)
    lfd f8, 0x58(r1)
    lfd f9, 0x60(r1)
    lfd f10, 0x68(r1)
    lfd f11, 0x70(r1)
    lfd f12, 0x78(r1)
    lfd f13, 0x80(r1)
    lwz r0, 0x10(r1)
    mtxer r0
    lwz r0, 0x0C(r1)
    mtctr r0
    lwz r0, 0x08(r1)
    mtcrf 0xFF, r0
    lwz r0, 0x94(r1)
    mtlr r0
    addi r1, r1, 0x90
    blr
}
