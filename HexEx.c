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

void HexExBeg(struct HexQ *const Qh) {
   Qh->Address = 0;
#ifndef HexFlatAddresses
   Qh->Segment = 0;
#endif
   Qh->Flags = 0, Qh->LineN = HexExLineN, Qh->Length = 0;
}

static char *AddByte(char *restrict HexP, const uint8_t Byte) {
   uint8_t HiNibble = (Byte&0xf0U) >> 4; *HexP++ = Hexit(HiNibble);
   uint8_t LoNibble = Byte&0x0fU; *HexP++ = Hexit(LoNibble);
   return HexP;
}

static char *AddWord(char *restrict HexP, const uint_fast16_t Word, uint8_t *const restrict SumP) {
   uint8_t HiByte = (Word >> 8)&0xffU, LoByte = Word&0xffU;
   *SumP += HiByte + LoByte;
   return AddByte(AddByte(HexP, (uint8_t)HiByte), (uint8_t)LoByte);
}

static char *AddNL(char *restrict HexP) {
   const char *restrict NL = HexNL;
   do *HexP++ = *NL++; while (*NL != '\0');
   return HexP;
}

static void PutEOF(struct HexQ *const Qh) {
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
   HexExFlush(Qh, HexExBuf, HexP);
}

static void PutSegment(struct HexQ *const Qh, const HexSegmentT Seg, const uint8_t Type) {
   char *restrict HexP = HexExBuf;
   uint8_t Sum = Type + 2U;
// ':', Length, Address msb:lsb, Record Type.
   *HexP++ = ':', HexP = AddByte(HexP, 2U), HexP = AddByte(HexP, 0), HexP = AddByte(HexP, 0), HexP = AddByte(HexP, Type);
// Addressed Line, CheckSum, NL.
   HexP = AddWord(HexP, Seg, &Sum), HexP = AddByte(HexP, (uint8_t)~Sum + 1U), HexP = AddNL(HexP);
   HexExFlush(Qh, HexExBuf, HexP);
}

// Write out Qh->Line.
static void PutHexLine(struct HexQ *const Qh) {
   uint_fast8_t Len = Qh->Length; if (Len == 0) return;
   if (Qh->Flags&HexAddrOverflowFlag) PutSegment(Qh, AddrPage(Qh->Address), HexAddrRec), Qh->Flags &= ~HexAddrOverflowFlag;
   char *restrict HexP = HexExBuf;
// ':', Length.
   *HexP++ = ':', HexP = AddByte(HexP, Len);
   uint8_t Sum = Len;
   Qh->Length = 0;
// 16-bit Address.
   uint_fast16_t Addr = Qh->Address&0xffffU;
   Qh->Address += Len;
// Signal an address overflow (need to write extended address).
   if (0xffffU - Addr < Len) Qh->Flags |= HexAddrOverflowFlag;
   HexP = AddWord(HexP, Addr, &Sum);
// Record Type.
   HexP = AddByte(HexP, HexLineRec);
#if 0
   Sum += HexLineRec; // HexLineRec is zero, so NOP.
#endif
// Addressed Line.
   uint8_t *restrict LineP = Qh->Line;
   do {
      uint8_t Byte = *LineP++;
      Sum += Byte, HexP = AddByte(HexP, Byte);
   } while (--Len > 0);
// CheckSum, NL.
   HexP = AddByte(HexP, ~Sum + 1U), HexP = AddNL(HexP);
   HexExFlush(Qh, HexExBuf, HexP);
}

void HexPutAtAddr(struct HexQ *const Qh, HexAddressT Addr) {
// Flush any existing data.
   if (Qh->Length > 0) PutHexLine(Qh);
   const HexAddressT Page = Addr&AddrPageMask;
// Write a new extended address if needed.
   if ((Qh->Address&AddrPageMask) != Page) Qh->Flags |= HexAddrOverflowFlag;
   else if (Qh->Address != Page) Qh->Flags &= ~HexAddrOverflowFlag;
   Qh->Address = Addr;
   HexSetLineN(Qh, Qh->LineN);
}

void HexSetLineN(struct HexQ *const Qh, uint8_t LineN) {
#if HexExLineMax < 0xff
   if (LineN > HexExLineMax) LineN = HexExLineMax;
   else
#endif
   if (LineN == 0) LineN = HexExLineN;
   Qh->LineN = LineN;
}

#ifndef HexFlatAddresses
void HexPutAtSeg(struct HexQ *const Qh, HexSegmentT Seg, HexAddressT Addr) {
   HexPutAtAddr(Qh, Addr);
   if (Qh->Segment != Seg) {
   // clear segment
      PutSegment(Qh, (Qh->Segment = Seg), HexSegRec);
   }
}
#endif

void HexPut1(struct HexQ *const Qh, const int Ch) {
   if (Qh->LineN <= Qh->Length) PutHexLine(Qh);
   Qh->Line[Qh->Length++] = (uint8_t)Ch;
}

void HexPut(struct HexQ *restrict const Qh, const void *restrict ExBuf, HexInt ExN) {
   const uint8_t *ExP = ExBuf;
   while (ExN > 0)
      if (Qh->LineN > Qh->Length) {
         uint_fast8_t dN = Qh->LineN - Qh->Length;
         uint8_t *HexP = Qh->Line + Qh->Length;
         dN = (HexInt)dN > ExN? (uint_fast8_t)ExN: dN;
         ExN -= dN, Qh->Length += dN;
         do *HexP++ = *ExP++; while (--dN > 0);
      } else PutHexLine(Qh);
}

void HexExEnd(struct HexQ *const Qh) {
   PutHexLine(Qh); // Flush any remaining data.
   PutEOF(Qh);
}
