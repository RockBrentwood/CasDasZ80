// A simple library for reading Intel HEX data.
// See the accompanying HexEx.h for IHEX write support.
//
// Reading Intel Hex data
// ──────────────────────
// To read data in the Intel HEX format, you must perform the actual reading of bytes using other means (e.g., stdio).
// The bytes read must then be passed to Get1 and/or Get.
// The reading functions will then call GetData, at which stage the HexIn class will contain the data along with its address.
// See below for details and example implementation of GetData.
//
// The sequence to read data in IHEX format is:
//	{ HexIn Q; Q->Get(InBuf, InN); }
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

#ifndef restrict
#   define restrict
#endif

#include "Hex.h"

struct HexIn: public HexQ {
// Begin reading at address Addr (the lowest 16 bits of which will be ignored), and at Seg (if segmentation is enabled).
// Addr is required only if the high bytes of the 32-bit starting address are not specified in the input data and they are non-zero.
// Seg is required only if the initial segment is not specified in the input data and it is non-zero.
#ifndef HexFlatAddresses
   HexIn(HexAddressT Addr = 0, HexSegmentT Seg = 0);
#else
   HexIn(HexAddressT Addr = 0);
#endif
// Read a single character
   void Get1(char Ch);
// Read InN bytes from InBuf.
   void Get(const char *restrict InBuf, HexInt InN);
// End reading (may call GetData if there is data waiting).
   ~HexIn();
// Called when a complete line has been read, the record type of which is passed as Type.
// The fields _Line, _LineN, _Address, and _Segment are set appropriately.
// In case of reading an HexAddrRec or an HexSegRec the record's data is not yet parsed -
// it will be parsed into the _Address or _Segment field only if GetData returns true.
// This allows manual handling of extended addresses by parsing the _Line bytes.
//
// Possible error cases include checksum mismatch (which is indicated as an argument), and excessive line length
// (in case this has been compiled with HexLineMax less than 0xff) which is indicated by _LineN greater than _Length.
// Unknown record types and other erroneous data is usually silently ignored by this minimalistic parser.
// (It is recommended to compute a hash over the complete data once received and verify that against the source.)
//
// Example implementation:
//	bool HexIn::GetData(HexRecordT Type, bool Error) {
//		Error = Error || _Length < _LineN;
//		if (Type == HexLineRec && !Error)
//			fseek(OutFile, HexAddress(), SEEK_SET), fwrite(_Line, 1, _Length, OutFile);
//		else if (Type == HexEndRec)
//			fclose(OutFile);
//		return !Error;
//	}
   bool GetData(HexRecordT Type, bool Error);
private:
   void HexInEnd();
};

#endif // !HexInH
