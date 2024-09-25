// A simple library for reading and writing the Intel HEX or IHEX format.
// Intended mainly for embedded systems, and thus somewhat optimised for size at the expense of error handling and generality.
//
// Usage
// ─────
// The library has been split into read and write parts, which use a common data class HexQ, but each can be used independently.
// Include the header HexIn.h for reading, and/or the header HexEx.h for writing (and link with their respective object files).
// Both can be used simultaneously - this header defines the shared data structures and definitions.
//
// Reading Intel Hex data
// ──────────────────────
// To read data in the Intel HEX format, you must perform the actual reading of bytes using other means (e.g., stdio).
// The HexQ class is specialized to HexIn.
// The bytes read must then be passed to Get1 and/or Get.
// The reading functions will then call GetData, at which stage the HexIn class will contain the data along with its address.
// See the header HexIn.h for details and example implementation of GetData.
//
// The sequence to read data in IHEX format is:
//	{ HexIn Q; Q.Get(InBuf, InN); }
//
// Writing binary data as Intel Hex
// ────────────────────────────────
// The HexQ class is specialized to HexEx.
// In order to write out data, the PutAtAddr or PutAtSeg functions are used to set the data location,
// and then the binary bytes are written with Put1 and/or Put.
// The writing functions will then call the function HexExFlush whenever the internal write buffer needs to be cleared -
// it is up to the caller to provide an implementation of HexExFlush to do the actual writing.
// See the header HexEx.h for details and an example implementation.
//
// See the declaration further down for an example implementation.
//
// The sequence to write data in IHEX format is:
//	{ HexEx Q; Q.PutAtAddr(0), Q.Put(ExBuf, ExN); }
// For outputs larger than 64KiB, 32-bit linear addresses are output.
// Normally the initial linear extended address record of zero is NOT written - it can be forced by setting _Flags |= HexAddrOverflowFlag before writing the first byte.
//
// Gaps in the data may be created by calling PutAtAddr with the new starting address without calling HexExEnd in between.
//
// The same HexEx may be used either for reading or writing, but NOT both at the same time.
// Furthermore, a global output buffer is used for writing, i.e., multiple threads must not write simultaneously (but multiple writes may be interleaved).
//
// Conserving memory
// ─────────────────
// For memory-critical use, you can save additional memory by defining HexLineMax as something less than 0xff.
// Note, however, that this limit affects both reading and writing, so the resulting library will be unable to read lines with more than this number of data bytes.
// That said, I haven't encountered any IHEX files with more than 32 data bytes per line.
// For write only there is no reason to define the maximum as greater than the line length you'll actually be writing, e.g., 32 or 16.
//
// If the write functionality is only occasionally used,
// you can provide your own buffer for the duration by defining HexNoExBuf and providing a char *HexExBuf
// which points to valid storage for at least HexExMax characters from before the first call to any IHEX write function to until after the last.
//
// If you are doing both reading and writing, you can define the maximum output length separately as HexExLineMax -
// this will decrease the write buffer size, but the HexEx will still use the larger HexLineMax for its data storage.
//
// You can also save a few additional bytes by disabling support for segmented addresses, by defining HexFlatAddresses.
// Both the read and write modules need to be build with the same option, as the resulting data structures will not be compatible otherwise.
// To be honest, this is a fairly pointless optimisation.
//
// Copyright (c) 2013-2019 Kimmo Kulovesi, https://arkku.com/
// Provided with absolutely no warranty, use at your own risk only.
// Use and distribute freely, mark modified copies as such.

#ifndef HexH
#define HexH

#define KK_IHEX_VERSION "2024-09-21"

#include <stdint.h>
#include <stdbool.h>
typedef uint_least32_t HexAddressT;
typedef uint_least16_t HexSegmentT;
typedef int HexInt;

// Maximum number of data bytes per line (applies to both reading and writing!); specify 0xff to support reading all possible lengths.
// Less can be used to limit memory footprint on embedded systems, e.g., most programs with IHEX output use 32.
#ifndef HexLineMax
#   define HexLineMax 0xff
#endif

enum HexFlags {
   HexAddrOverflowFlag = 0x80 // 16-bit address overflow
};
typedef uint8_t HexFlagsT;

struct HexQ {
   HexAddressT _Address;
#ifndef HexFlatAddresses
   HexSegmentT _Segment;
// Resolve segmented address (if any).
// It is the author's recommendation that segmented addressing not be used (and indeed the write function of this library uses linear 32-bit addressing unless manually overridden).
   HexAddressT HexAddress() { return _Address + (((HexAddressT)_Segment) << 4); }
// Note that segmented addressing with the above macro is not strictly adherent to the IHEX specification,
// which mandates that the lowest 16 bits of the address and the index of the data byte must be added modulo 64K
// (i.e., at 16 bits precision with wraparound) and the segment address only added afterwards.
//
// To implement fully correct segmented addressing, compute the address of _each byte_ with its index in _Line as follows:
   HexAddressT HexByteAddress(HexInt Index) { return ((_Address + Index)&0xffffU) + (((HexAddressT)_Segment) << 4); }
#else // HexFlatAddresses:
   HexAddressT HexAddress() { return _Address; }
   HexAddressT HexByteAddress(HexInt Index) { return _Address + Index; }
#endif
   HexFlagsT _Flags;
   uint8_t _LineN, _Length, _Line[HexLineMax + 1];
};

enum HexRecord {
   HexLineRec, HexEndRec,
   HexSegRec, HexStartSegRec,
   HexAddrRec, HexStartAddrRec
};
typedef uint8_t HexRecordT;

// The newline string (appended to every output line, e.g., "\r\n").
#ifndef HexNL
#   define HexNL "\n"
#endif

// See HexIn.h and HexEx.h for function declarations!

#endif // !HexH
