// A simple library for reading Intel HEX data.
// See the accompanying HexEx.h for IHEX write support.
//
// Reading Intel Hex data
// ──────────────────────
// To read data in the Intel HEX format, you must perform the actual reading of bytes using other means (e.g., stdio).
// The bytes read must then be passed to HexGet1 and/or HexGet.
// The reading functions will then call HexGetData, at which stage the struct HexQ structure will contain the data along with its address.
// See below for details and example implementation of HexGetData.
//
// The sequence to read data in IHEX format is:
//	struct HexQ Qb;
//	HexInBeg(&Qb);
//	HexGet(&Qb, InBuf, InN);
//	HexInEnd(&Qb);
//
// Conserving memory
// ─────────────────
// For memory-critical use, you can save additional memory by defining HexLineMax as something less than 0xff.
// Note, however, that this limit affects both reading and writing, so the resulting library will be unable to read lines with more than this number of data bytes.
// That said, I haven't encountered any IHEX files with more than 32 data bytes per line.
//
// Copyright (c) 2013-2019 Kimmo Kulovesi, https://arkku.com/
// Provided with absolutely no warranty, use at your own risk only.
// Use and distribute freely, mark modified copies as such.

#ifndef HexInH
#define HexInH

#ifdef __cplusplus
#   ifndef restrict
#      define restrict
#   endif
extern "C" {
#else
#   define restrict
#endif

#include "Hex.h"

// Begin reading at address 0.
void HexInBeg(struct HexQ *Qh);

// Begin reading at Addr (the lowest 16 bits of which will be ignored);
// this is required only if the high bytes of the 32-bit starting address are not specified in the input data and they are non-zero.
void HexGetAtAddr(struct HexQ *Qh, HexAddressT Addr);

// Read a single character
void HexGet1(struct HexQ *Qh, char Ch);

// Read InN bytes from InBuf.
void HexGet(struct HexQ *restrict Qh, const char *restrict InBuf, HexInt InN);

// End reading (may call HexGetData if there is data waiting).
void HexInEnd(struct HexQ *Qh);

// Called when a complete line has been read, the record type of which is passed as Type.
// The Qh structure will have its fields Line, LineN, Address, and Segment set appropriately.
// In case of reading an HexAddrRec or an HexSegRec the record's data is not yet parsed -
// it will be parsed into the Address or Segment field only if HexGetData returns true.
// This allows manual handling of extended addresses by parsing the Qh->Line bytes.
//
// Possible error cases include checksum mismatch (which is indicated as an argument), and excessive line length
// (in case this has been compiled with HexLineMax less than 0xff) which is indicated by LineN greater than Length.
// Unknown record types and other erroneous data is usually silently ignored by this minimalistic parser.
// (It is recommended to compute a hash over the complete data once received and verify that against the source.)
//
// Example implementation:
//	HexBool HexGetData(struct HexQ *Qh, HexRecordT Type, HexBool Error) {
//		Error = Error || Qh->Length < Qh->LineN;
//		if (Type == HexLineRec && !Error)
//			fseek(OutFile, HexAddress(Qh), SEEK_SET), fwrite(Qh->Line, 1, Qh->Length, OutFile);
//		else if (Type == HexEndRec)
//			fclose(OutFile);
//		return !Error;
//	}
HexBool HexGetData(struct HexQ *Qh, HexRecordT Type, HexBool Error);

// Begin reading at Seg; this is required only if the initial segment is not specified in the input data and it is non-zero.
#ifndef HexFlatAddresses
void HexGetAtSeg(struct HexQ *Qh, HexSegmentT Seg);
#endif

#ifdef __cplusplus
}
#endif

#endif // !HexInH
