// A simple library for writing Intel HEX data.
// See the accompanying HexIn.h for read support, and the main header Hex.h for the shared parts.
//
// Writing binary data as Intel Hex
// ────────────────────────────────
// In order to write out data, the HexPutAtAddr or HexPutAtSeg functions are used to set the data location,
// and then the binary bytes are written with HexPut1 and/or HexPut.
// The writing functions will then call the function HexExFlush whenever the internal write buffer needs to be cleared -
// it is up to the caller to provide an implementation of HexExFlush to do the actual writing.
// See below for details and an example implementation.
//
// See the declaration further down for an example implementation.
//
// The sequence to write data in IHEX format is:
//	struct HexQ Qb;
//	HexExBeg(&Qb);
//	HexPutAtAddr(&Qb, 0);
//	HexPut(&Qb, ExBuf, ExN);
//	HexExEnd(&Qb);
// For outputs larger than 64KiB, 32-bit linear addresses are output.
// Normally the initial linear extended address record of zero is NOT written - it can be forced by setting Qh->flags |= HexAddrOverflowFlag before writing the first byte.
//
// Gaps in the data may be created by calling HexPutAtAddr with the new starting address without calling HexExEnd in between.
//
// The same struct HexQ may be used either for reading or writing, but NOT both at the same time.
// Furthermore, a global output buffer is used for writing, i.e., multiple threads must not write simultaneously (but multiple writes may be interleaved).
//
// Conserving memory
// ─────────────────
// If you are using only the write support, you should define HexLineMax as the length of your output line.
// This makes both the struct HexQ and the internal write buffer smaller.
// For example, 32 or even 16 can be used instead of the default 0xff.
//
// If the write functionality is not used all the time and can thus share its write buffer memory with something else that is inactive during writing IHEX, you can define HexNoExBuf and provide the buffer as char *HexExBuf.
// The size of the buffer must be at least HexExMax bytes and it must be valid for the entire duration from the first call to a write function until after the last call to HexExEnd.
// Note that there is no advantage to this unless something else, mutually exclusive with IHEX writing, can share the memory.
//
// If you are reading IHEX as well, then you'll end up limiting the maximum length of line that can be read.
// In that case you may wish to define HexExLineMax as smaller to decrease the write buffer size, but keep HexLineMax at 0xff to support reading any IHEX file.
//
// Copyright (c) 2013-2019 Kimmo Kulovesi, https://arkku.com/
// Provided with absolutely no warranty, use at your own risk only.
// Use and distribute freely, mark modified copies as such.

#ifndef HexExH
#define HexExH

#ifdef __cplusplus
#   ifndef restrict
#      define restrict
#   endif
extern "C" {
#else
#   define restrict
#endif

#include "Hex.h"

// Default number of data bytes written per line
#if HexLineMax >= 0x20
#   define HexExLineN 0x20
#else
#   define HexExLineN HexLineMax
#endif

#ifndef HexExLineMax
#   define HexExLineMax HexLineMax
#endif

// Length of the write buffer required
#define HexExMax (1 + 2 + 4 + 2 + (HexExLineMax*2) + 2 + sizeof HexNL)

#ifdef HexNoExBuf
// Define HexNoExBuf to provide an external write buffer, as char *HexExBuf,
// which must point to a valid storage for at least HexExMax characters whenever any of the write functionality is used (see above under "CONSERVING MEMORY").
extern char *HexExBuf;
#endif

// Initialise the structure Qh for writing.
void HexExBeg(struct HexQ *Qh);

// Begin writing at the given 32-bit Addr after writing any pending data at the current address.
// This can also be used to skip to a new address without calling HexExEnd; this allows writing sparse output.
void HexPutAtAddr(struct HexQ *Qh, HexAddressT Addr);

// Write a single byte.
void HexPut1(struct HexQ *Qh, int Ch);

// Write ExN bytes from ExBuf.
void HexPut(struct HexQ *restrict Qh, const void *restrict ExBuf, HexInt ExN);

// End writing (flush buffers, write end of file record).
void HexExEnd(struct HexQ *Qh);

// Called whenever the global, internal write buffer needs to be flushed by the write functions.
// The implementation is NOT provided by this library; this must be implemented to perform the actual output,
// i.e., write out (EndP - ExBuf) bytes from ExBuf (which is not NUL-terminated, but may be modified to make it thus).
//
// Example implementation:
//	void HexExFlush(struct HexQ *Qh, char *ExBuf, char *EndP) {
//		*EndP = '\0', fputs(ExBuf, stdout);
//	}
// Note that the contents of ExBuf can become invalid immediately after this function returns - the data must be copied if it needs to be preserved!
void HexExFlush(struct HexQ *Qh, char *ExBuf, char *EndP);

// As HexPutAtAddr, but specify a segment selector.
// Note that segments are not automatically incremented when the 16-bit address overflows (the default is to use 32-bit linear addressing).
// For segmented 20-bit addressing you must manually ensure that a write does not overflow the segment boundary, and call HexPutAtSeg every time the segment needs to be changed.
#ifndef HexFlatAddresses
void HexPutAtSeg(struct HexQ *Qh, HexSegmentT Seg, HexAddressT Addr);
#endif

// Set the output line length to LineN - may be safely called only right after HexPutAtAddr or HexPutAtSeg.
// The maximum is HexLineMax (which may be changed at compile time).
void HexSetLineN(struct HexQ *Qh, uint8_t LineN);

#ifdef __cplusplus
}
#endif

#endif // !HexExH
