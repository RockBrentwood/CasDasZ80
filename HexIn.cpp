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

#ifndef HexFlatAddresses
HexIn::HexIn(HexAddressT Addr/* = 0*/, HexSegmentT Seg/* = 0*/) {
#else
HexIn::HexIn(HexAddressT Addr/* = 0*/) {
#endif
   _Address = Addr;
#ifndef HexFlatAddresses
   _Segment = Seg;
#endif
   _Flags = 0, _LineN = 0, _Length = 0;
}

void HexIn::HexInEnd() {
   uint_fast8_t Type = _Flags&InTypeMask, Sum = _Length;
   if (Sum == 0 && Type == HexLineRec) return;
// Compute and validate the checksum.
   const uint8_t *const EndP = _Line + Sum;
   Sum += Type + (_Address&0xffU) + ((_Address >> 8)&0xffU);
   for (const uint8_t *LineP = _Line; LineP != EndP; LineP++) Sum += *LineP;
   Sum = (~Sum + 1U) ^ *EndP; // *EndP is the received checksum
   if (GetData(Type, (uint8_t)Sum)) {
      if (Type == HexAddrRec) _Address &= 0xffffU, _Address |= (((HexAddressT)_Line[0]) << 24) | (((HexAddressT)_Line[1]) << 16);
#ifndef HexFlatAddresses
      else if (Type == HexSegRec) _Segment = (HexSegmentT)((_Line[0] << 8) | _Line[1]);
#endif
   }
   _Length = 0, _Flags = 0;
}

HexIn::~HexIn() { HexInEnd(); }

void HexIn::Get1(const char Ch) {
   uint_fast8_t B = (uint_fast8_t)Ch;
   uint_fast8_t LineN = _Length;
   uint_fast8_t InQ = _Flags&InMask;
   _Flags ^= InQ; // Turn off the old state.
   InQ >>= InOffset;
   if (B >= '0' && B <= '9') B -= '0';
   else if (B >= 'A' && B <= 'F') B -= 'A' - 10;
   else if (B >= 'a' && B <= 'f') B -= 'a' - 10;
// Sync to a new record at any state.
   else if (B == ':') { InQ = InCount0Q; goto EndIn; }
// Ignore unknown characters (e.g., extra whitespace).
   else goto SaveInQ;
// High nibble: store temporarily at end of data.
   if (!(++InQ&1)) B <<= 4, _Line[LineN] = B;
// Low nibble: combine with stored high nibble.
   else {
      B = _Line[LineN] |= B;
   // We already know the lowest bit of InQ, dropping it may produce smaller code, hence the `>> 1` in switch and its cases.
      switch (InQ >> 1) {
      // Remain in the initial state while waiting for ':'.
         default: return;
      // The data length.
         case InCount1Q >> 1:
            _LineN = B;
#if HexLineMax < 0xff
            if (B > HexLineMax) { HexInEnd(); return; }
#endif
         break;
      // The high byte of the 16-bit address: clear and reset the 16-bit address.
         case InAddr01Q >> 1: _Address &= InAddrHiMask, _Address |= ((HexAddressT)B) << 8U; break;
      // The low byte of the 16-bit address.
         case InAddr11Q >> 1: _Address |= (HexAddressT)B; break;
      // The record type.
         case InType1Q >> 1:
         // Skip unknown record types silently.
            if (B&~InTypeMask) return;
            _Flags = (_Flags&~InTypeMask) | B;
         break;
         case InData1Q >> 1:
            if (LineN < _LineN) {
            // A data byte.
               _Length = LineN + 1, InQ = InData0Q;
               goto SaveInQ;
            }
         // The end of line (last "Line" byte is the checksum).
            InQ = BegQ;
         EndIn:
            HexInEnd();
      }
   }
SaveInQ:
   _Flags |= InQ << InOffset;
}

void HexIn::Get(const char *restrict InBuf, HexInt InN) {
   for (; InN > 0; InN--) Get1(*InBuf++);
}
