// A simple library for reading the Intel HEX (IHEX) format.
// See the header Hex.h for instructions.
//
// Copyright (c) 2013-2019 Kimmo Kulovesi, https://arkku.com/
// Provided with absolutely no warranty, use at your own risk only.
// Use and distribute freely, mark modified copies as such.

#include "HexIn.h"

#define InAddrHiMask ((HexAddressT)0xffff0000U)

enum HexInQ {
   BegQ = 0,
   InCount0Q = 1, InCount1Q,
   InAddr00Q, InAddr01Q, InAddr10Q, InAddr11Q,
   InType0Q, InType1Q, InData0Q, InData1Q
};

#define InTypeMask 0x07
#define InMask 0x78
#define InOffset 3

void HexInBeg(struct HexQ *const Qh) {
   Qh->Address = 0;
#ifndef HexFlatAddresses
   Qh->Segment = 0;
#endif
   Qh->Flags = 0, Qh->LineN = 0, Qh->Length = 0;
}

void HexGetAtAddr(struct HexQ *const Qh, HexAddressT Addr) {
   HexInBeg(Qh);
   Qh->Address = Addr;
}

#ifndef HexFlatAddresses
void HexGetAtSeg(struct HexQ *const Qh, HexSegmentT Seg) {
   HexInBeg(Qh);
   Qh->Segment = Seg;
}
#endif

void HexInEnd(struct HexQ *const Qh) {
   uint_fast8_t Type = Qh->Flags&InTypeMask, Sum = Qh->Length;
   if (Sum == 0 && Type == HexLineRec) return;
// Compute and validate the checksum.
   const uint8_t *const EndP = Qh->Line + Sum;
   Sum += Type + (Qh->Address&0xffU) + ((Qh->Address >> 8)&0xffU);
   for (const uint8_t *LineP = Qh->Line; LineP != EndP; LineP++) Sum += *LineP;
   Sum = (~Sum + 1U) ^ *EndP; // *EndP is the received checksum
   if (HexGetData(Qh, Type, (uint8_t)Sum)) {
      if (Type == HexAddrRec) Qh->Address &= 0xffffU, Qh->Address |= (((HexAddressT)Qh->Line[0]) << 24) | (((HexAddressT)Qh->Line[1]) << 16);
#ifndef HexFlatAddresses
      else if (Type == HexSegRec) Qh->Segment = (HexSegmentT)((Qh->Line[0] << 8) | Qh->Line[1]);
#endif
   }
   Qh->Length = 0, Qh->Flags = 0;
}

void HexGet1(struct HexQ *const Qh, const char Ch) {
   uint_fast8_t B = (uint_fast8_t)Ch;
   uint_fast8_t LineN = Qh->Length;
   uint_fast8_t InQ = (Qh->Flags&InMask);
   Qh->Flags ^= InQ; // Turn off the old state.
   InQ >>= InOffset;
   if (B >= '0' && B <= '9') B -= '0';
   else if (B >= 'A' && B <= 'F') B -= 'A' - 10;
   else if (B >= 'a' && B <= 'f') B -= 'a' - 10;
// Sync to a new record at any state.
   else if (B == ':') { InQ = InCount0Q; goto EndIn; }
// Ignore unknown characters (e.g., extra whitespace).
   else goto SaveInQ;
// High nibble: store temporarily at end of data.
   if (!(++InQ&1)) B <<= 4, Qh->Line[LineN] = B;
// Low nibble: combine with stored high nibble.
   else {
      B = Qh->Line[LineN] |= B;
   // We already know the lowest bit of InQ, dropping it may produce smaller code, hence the `>> 1` in switch and its cases.
      switch (InQ >> 1) {
      // Remain in the initial state while waiting for ':'.
         default: return;
      // The data length.
         case InCount1Q >> 1:
            Qh->LineN = B;
#if HexLineMax < 0xff
            if (B > HexLineMax) { HexInEnd(Qh); return; }
#endif
         break;
      // The high byte of the 16-bit address: clear and reset the 16-bit address.
         case InAddr01Q >> 1: Qh->Address &= InAddrHiMask, Qh->Address |= ((HexAddressT)B) << 8U; break;
      // The low byte of the 16-bit address.
         case InAddr11Q >> 1: Qh->Address |= (HexAddressT)B; break;
      // The record type.
         case InType1Q >> 1:
         // Skip unknown record types silently.
            if (B&~InTypeMask) return;
            Qh->Flags = (Qh->Flags&~InTypeMask) | B;
         break;
         case InData1Q >> 1:
            if (LineN < Qh->LineN) {
            // A data byte.
               Qh->Length = LineN + 1, InQ = InData0Q;
               goto SaveInQ;
            }
         // The end of line (last "Line" byte is the checksum).
            InQ = BegQ;
         EndIn:
            HexInEnd(Qh);
      }
   }
SaveInQ:
   Qh->Flags |= InQ << InOffset;
}

void HexGet(struct HexQ *restrict Qh, const char *restrict InBuf, HexInt InN) {
   for (; InN > 0; InN--) HexGet1(Qh, *InBuf++);
}
