// A simple library for writing the Intel HEX (IHEX) format.
// See the header Hex.h for instructions.
//
// Copyright (c) 2013-2019 Kimmo Kulovesi, https://arkku.com/
// Provided with absolutely no warranty, use at your own risk only.
// Use and distribute freely, mark modified copies as such.

#include "HexEx.h"

#define AddrPageMask ((HexAddressT)0xffff0000U)
#define AddrPage(Addr) ((Addr) >> 16)
#define Hexit(N) ((char)((N) + ((N) < 10? '0': 'A' - 10)))

#ifndef HexNoExBuf
static char HexExBuf[HexExMax];
#endif
#if HexExLineMax > HexLineMax
#   error "HexExLineMax > HexLineMax"
#endif

HexEx::HexEx() {
   _Address = 0;
#ifndef HexFlatAddresses
   _Segment = 0;
#endif
   _Flags = 0, _LineN = HexExLineN, _Length = 0;
}

static char *AddByte(char *restrict HexP, const uint8_t Byte) {
   uint8_t HiNibble = (Byte&0xf0U) >> 4; *HexP++ = Hexit(HiNibble);
   uint8_t LoNibble = Byte&0x0fU; *HexP++ = Hexit(LoNibble);
   return HexP;
}

static char *AddWord(char *restrict HexP, const uint_fast16_t Word, uint8_t &Sum) {
   uint8_t HiByte = (Word >> 8)&0xffU, LoByte = Word&0xffU;
   Sum += HiByte + LoByte;
   return AddByte(AddByte(HexP, (uint8_t)HiByte), (uint8_t)LoByte);
}

static char *AddNL(char *restrict HexP) {
   const char *restrict NL = HexNL;
   do *HexP++ = *NL++; while (*NL != '\0');
   return HexP;
}

void HexEx::PutEOF() {
   char *restrict HexP = HexExBuf;
// ':', Length, Address msb:lsb, Record Type, CheckSum.
   *HexP++ = ':';
#if 1
   *HexP++ = '0', *HexP++ = '0';
   *HexP++ = '0', *HexP++ = '0', *HexP++ = '0', *HexP++ = '0';
   *HexP++ = '0', *HexP++ = '1';
   *HexP++ = 'F', *HexP++ = 'F';
#else
   HexP = AddByte(HexP, 0);
   HexP = AddByte(HexP, 0), HexP = AddByte(HexP, 0);
   HexP = AddByte(HexP, HexEndRec);
   HexP = AddByte(HexP, (uint8_t)~HexEndRec + 1U);
#endif
   HexP = AddNL(HexP);
   Flush(HexExBuf, HexP);
}

void HexEx::PutSegment(const HexSegmentT Seg, const uint8_t Type) {
   char *restrict HexP = HexExBuf;
   uint8_t Sum = Type + 2U;
// ':', Length, Address msb:lsb, Record Type.
   *HexP++ = ':', HexP = AddByte(HexP, 2U), HexP = AddByte(HexP, 0), HexP = AddByte(HexP, 0), HexP = AddByte(HexP, Type);
// Addressed Line, CheckSum, NL.
   HexP = AddWord(HexP, Seg, Sum), HexP = AddByte(HexP, (uint8_t)~Sum + 1U), HexP = AddNL(HexP);
   Flush(HexExBuf, HexP);
}

// Write out _Line.
void HexEx::PutHexLine() {
   uint_fast8_t Len = _Length; if (Len == 0) return;
   if (_Flags&HexAddrOverflowFlag) PutSegment(AddrPage(_Address), HexAddrRec), _Flags &= ~HexAddrOverflowFlag;
   char *restrict HexP = HexExBuf;
// ':', Length.
   *HexP++ = ':', HexP = AddByte(HexP, Len);
   uint8_t Sum = Len;
   _Length = 0;
// 16-bit Address.
   uint_fast16_t Addr = _Address&0xffffU;
   _Address += Len;
// Signal an address overflow (need to write extended address).
   if (0xffffU - Addr < Len) _Flags |= HexAddrOverflowFlag;
   HexP = AddWord(HexP, Addr, Sum);
// Record Type.
   HexP = AddByte(HexP, HexLineRec);
#if 0
   Sum += HexLineRec; // HexLineRec is zero, so NOP.
#endif
// Addressed Line.
   uint8_t *restrict LineP = _Line;
   do {
      uint8_t Byte = *LineP++;
      Sum += Byte, HexP = AddByte(HexP, Byte);
   } while (--Len > 0);
// CheckSum, NL.
   HexP = AddByte(HexP, ~Sum + 1U), HexP = AddNL(HexP);
   Flush(HexExBuf, HexP);
}

void HexEx::PutAtAddr(HexAddressT Addr) {
// Flush any existing data.
   if (_Length > 0) PutHexLine();
   const HexAddressT Page = Addr&AddrPageMask;
// Write a new extended address if needed.
   if ((_Address&AddrPageMask) != Page) _Flags |= HexAddrOverflowFlag;
   else if (_Address != Page) _Flags &= ~HexAddrOverflowFlag;
   _Address = Addr;
   SetLineN(_LineN);
}

void HexEx::SetLineN(uint8_t LineN) {
#if HexExLineMax < 0xff
   if (LineN > HexExLineMax) LineN = HexExLineMax;
   else
#endif
   if (LineN == 0) LineN = HexExLineN;
   _LineN = LineN;
}

#ifndef HexFlatAddresses
void HexEx::PutAtSeg(HexSegmentT Seg, HexAddressT Addr) {
   PutAtAddr(Addr);
   if (_Segment != Seg) {
   // clear segment
      PutSegment((_Segment = Seg), HexSegRec);
   }
}
#endif

void HexEx::Put1(const int Ch) {
   if (_LineN <= _Length) PutHexLine();
   _Line[_Length++] = (uint8_t)Ch;
}

void HexEx::Put(const uint8_t *restrict ExBuf, HexInt ExN) {
   while (ExN > 0)
      if (_LineN > _Length) {
         uint_fast8_t dN = _LineN - _Length;
         uint8_t *HexP = _Line + _Length;
         dN = (HexInt)dN > ExN? (uint_fast8_t)ExN: dN;
         ExN -= dN, _Length += dN;
         do *HexP++ = *ExBuf++; while (--dN > 0);
      } else PutHexLine();
}

HexEx::~HexEx() {
   PutHexLine(); // Flush any remaining data.
   PutEOF();
}
