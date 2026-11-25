#pragma once

// Inline-hook stubs save r0, r3-r31 and LR but not CR, CTR, XER, FPSCR or
// any FPR. Every hook therefore enters through a short asm entry that loads
// its C++ body into r12 and branches to tmPreservingCall, which saves the
// volatile ones of those, calls the body and restores them, so the hooked
// code never sees them change, whatever the site. r12 is free there: the
// stub restores r3-r31 itself after the hook returns.
//
//   static asm void myEntry()
//   {
//       nofralloc
//       lis r12, tmMyBody@h
//       ori r12, r12, tmMyBody@l
//       b tmPreservingCall
//   }
extern "C" void tmPreservingCall();
