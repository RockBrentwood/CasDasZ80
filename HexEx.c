// HexEx.c: A simple library for writing the Intel HEX (IHEX) format.
// See the header Hex.h for instructions.
//
// Copyright (c) 2013-2019 Kimmo Kulovesi, https://arkku.com/
// Provided with absolutely no warranty, use at your own risk only.
// Use and distribute freely, mark modified copies as such.

#include "HexEx.h"

#define HexStart ':'
#define AddrPageMask ((HexAddressT)0xffff0000U)
#define AddrPage(Addr) ((Addr) >> 16)
#define Hexit(N) ((char)((N) + (((N) < 10)? '0': ('A' - 10))))

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
   Qh->Flags = 0;
   Qh->LineN = HexExLineN;
   Qh->Length = 0;
}

static char *AddByte(char *restrict HexP, const uint8_t Byte) {
   uint8_t N = (Byte&0xf0U) >> 4; // high nybble
   *HexP++ = Hexit(N);
   N = Byte&0x0fU; // low nybble
   *HexP++ = Hexit(N);
   return HexP;
}

static char *AddWord(char *restrict HexP, const uint_fast16_t Word, uint8_t *const restrict SumP) {
   uint8_t Byte = (Word >> 8)&0xffU; // high byte
   HexP = AddByte(HexP, (uint8_t)Byte);
   *SumP += Byte;
   Byte = Word&0xffU; // low byte
   *SumP += Byte;
   return AddByte(HexP, (uint8_t)Byte);
}

static char *AddNL(char *restrict HexP) {
   const char *restrict NL = HexNL;
   do {
      *HexP++ = *NL++;
   } while (*NL);
   return HexP;
}

static void PutEOF(struct HexQ *const Qh) {
   char *restrict HexP = HexExBuf;
   *HexP++ = HexStart; // :
#if 1
   *HexP++ = '0';
   *HexP++ = '0'; // length
   *HexP++ = '0';
   *HexP++ = '0';
   *HexP++ = '0';
   *HexP++ = '0'; // address
   *HexP++ = '0';
   *HexP++ = '1'; // record type
   *HexP++ = 'F';
   *HexP++ = 'F'; // checksum
#else
   HexP = AddByte(HexP, 0); // length
   HexP = AddByte(HexP, 0); // address msb
   HexP = AddByte(HexP, 0); // address lsb
   HexP = AddByte(HexP, HexEndRec); // record type
   HexP = AddByte(HexP, (uint8_t)~HexEndRec + 1U); // checksum
#endif
   HexP = AddNL(HexP);
   HexExFlush(Qh, HexExBuf, HexP);
}

static void PutSegment(struct HexQ *const Qh, const HexSegmentT Seg, const uint8_t Type) {
   char *restrict HexP = HexExBuf;
   uint8_t Sum = Type + 2U;
   *HexP++ = HexStart; // :
   HexP = AddByte(HexP, 2U); // length
   HexP = AddByte(HexP, 0); // 16-bit address msb
   HexP = AddByte(HexP, 0); // 16-bit address lsb
   HexP = AddByte(HexP, Type); // record type
   HexP = AddWord(HexP, Seg, &Sum); // high bytes of address
   HexP = AddByte(HexP, (uint8_t)~Sum + 1U); // checksum
   HexP = AddNL(HexP);
   HexExFlush(Qh, HexExBuf, HexP);
}

// Write out Qh->Line
static void PutHexLine(struct HexQ *const Qh) {
   uint_fast8_t Len = Qh->Length;
   if (!Len) {
      return;
   }
   if (Qh->Flags&HexAddrOverflowFlag) {
      PutSegment(Qh, AddrPage(Qh->Address), HexAddrRec);
      Qh->Flags &= ~HexAddrOverflowFlag;
   }
   char *restrict HexP = HexExBuf;
// :
   *HexP++ = HexStart;
// length
   HexP = AddByte(HexP, Len);
   uint8_t Sum = Len;
   Qh->Length = 0;
// 16-bit address
   uint_fast16_t Addr = Qh->Address&0xffffU;
   Qh->Address += Len;
   if ((0xffffU - Addr) < Len) {
   // signal address overflow (need to write extended address)
      Qh->Flags |= HexAddrOverflowFlag;
   }
   HexP = AddWord(HexP, Addr, &Sum);
// record type
   HexP = AddByte(HexP, HexLineRec);
#if 0
   Sum += HexLineRec; // HexLineRec is zero, so NOP
#endif
// data
   uint8_t *restrict LineP = Qh->Line;
   do {
      uint8_t Byte = *LineP++;
      Sum += Byte;
      HexP = AddByte(HexP, Byte);
   } while (--Len);
// checksum
   HexP = AddByte(HexP, ~Sum + 1U);
   HexP = AddNL(HexP);
   HexExFlush(Qh, HexExBuf, HexP);
}

void HexPutAtAddr(struct HexQ *const Qh, HexAddressT Addr) {
   if (Qh->Length) {
   // flush any existing data
      PutHexLine(Qh);
   }
   const HexAddressT Page = Addr&AddrPageMask;
   if ((Qh->Address&AddrPageMask) != Page) {
   // write a new extended address if needed
      Qh->Flags |= HexAddrOverflowFlag;
   } else if (Qh->Address != Page) {
      Qh->Flags &= ~HexAddrOverflowFlag;
   }
   Qh->Address = Addr;
   HexSetLineN(Qh, Qh->LineN);
}

void HexSetLineN(struct HexQ *const Qh, uint8_t LineN) {
#if HexExLineMax < 0xff
   if (LineN > HexExLineMax) {
      LineN = HexExLineMax;
   } else
#endif
   if (!LineN) {
      LineN = HexExLineN;
   }
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
   if (Qh->LineN <= Qh->Length) {
      PutHexLine(Qh);
   }
   Qh->Line[(Qh->Length)++] = (uint8_t)Ch;
}

void HexPut(struct HexQ *restrict const Qh, const void *restrict ExBuf, HexInt ExN) {
   const uint8_t *ExP = ExBuf;
   while (ExN > 0) {
      if (Qh->LineN > Qh->Length) {
         uint_fast8_t dN = Qh->LineN - Qh->Length;
         uint8_t *HexP = Qh->Line + Qh->Length;
         dN = ((HexInt)dN > ExN)? (uint_fast8_t)ExN: dN;
         ExN -= dN;
         Qh->Length += dN;
         do {
            *HexP++ = *ExP++;
         } while (--dN);
      } else {
         PutHexLine(Qh);
      }
   }
}

void HexExEnd(struct HexQ *const Qh) {
   PutHexLine(Qh); // flush any remaining data
   PutEOF(Qh);
}
