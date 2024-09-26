// Z80 Assembler.
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include "HexEx.h"
#include "Cas.h"

uint32_t CurPC;	// The current address.
uint8_t *RAM;	// The 64K RAM of the Z80.
static const uint32_t MaxRAM = 0x10000;
static uint32_t LoPC = MaxRAM, HiPC = 0;
static bool Listing = false;
static FILE *AsmF, *BinF, *Z80F, *HexF;
int Loudness = 0;
static long LineNo; // The current line number.
static char LineBuf[LineMax]; // A buffer for the current line.

// Print a fatal error message and exit.
void Error(const char *Message) {
   printf("Error in line %ld: %s\n", LineNo, Message);
   const char *p;
   for (p = LineBuf; isspace(*p); p++);
   puts(p);
   exit(1);
}

static void Usage(const char *Path) {
   const char *App = Path;
   for (char Ch; (Ch = *Path++) != '\0'; ) if (Ch == '/' || Ch == '\\') App = Path;
   printf(
      "Usage: %s [-l] [-n] [-v] INFILE\n"
      "  -c       CP/M com file format for binary\n"
      "  -fXX     fill ram with byte XX (default: 00)\n"
      "  -l       show listing\n"
      "  -n       no output files\n"
      "  -oXXXX   offset address = 0x0000 .. 0xFFFF\n"
      "  -v       increase verbosity\n",
      App
   );
}

// Create a listing for one source code line.
//	Address    Data Bytes    Source Code
// Break long data block (e.g. defm) into lines of 4 data bytes.
static void ListOneLine(uint32_t BegPC, uint32_t EndPC, const char *Line) {
   if (!Listing) return;
   if (BegPC == EndPC) printf("%*s\n", 24 + int(strlen(Line)), Line);
   else {
      printf("%4.4X   ", BegPC);
      uint32_t PC = BegPC;
      int n = 0;
      while (PC < EndPC) {
         printf(" %2.2X", RAM[PC++]);
         if (n == 3) printf("     %s", Line);
         if ((n&3) == 3) {
            printf("\n");
            if (PC < EndPC) printf("%4.4X   ", PC);
         }
         n++;
      }
      if (n < 4) printf("%*s\n", 5 + 3*(4 - n) + int(strlen(Line)), Line);
      else if ((n&3) != 0) printf("\n");
   }
}

void Log(int Loud, const char *Format, ...) {
   if (Loudness >= Loud) {
      while (Loud-- > 0) fprintf(stderr, " ");
      va_list AP; va_start(AP, Format), vfprintf(stderr, Format, AP), va_end(AP);
   }
}

void List(const char *Format, ...) {
   if (Listing) {
      va_list AP; va_start(AP, Format), vprintf(Format, AP), va_end(AP);
   }
}

// The Z80 format is used by the Z80-asm.
// http://wwwhomes.uni-bielefeld.de/achim/z80-asm.html
// *.Z80 files are bin files with a header telling the bin offset
//	struct z80_header {
//		const char *Signature = "Z80ASM" "\032" "\n";
//		uint16_t Offset;
//	}
static void PutHeader(FILE *ExF, uint32_t Addr) {
   const char *Signature = "Z80ASM" "\032" "\n";
   unsigned char Buf[2]; Buf[0] = Addr&0xff, Buf[1] = Addr >> 8;
   fwrite(Signature, 1, strlen(Signature), ExF), fwrite(Buf, 1, 2, ExF);
}

int main(int AC, char **AV) {
   char *InFile = nullptr;
   bool IsCom = false;
   int BasePC = 0;
   int Fill = 0;
   bool NoAsmF = false;
   fprintf(stderr, "CasZ80 - a small 1-pass assembler for Z80 code\n");
   fprintf(stderr, "Based on TurboAss Z80 (c)1992-1993 Sigma-Soft, Markus Fritze\n");
   for (int A = 1, Ax = 0; A < AC; A++)
      if ('-' == AV[A][0]) {
         switch (AV[A][++Ax]) {
         // Create a CP/M com file.
            case 'c': IsCom = true; break;
         // Fill.
            case 'f': {
               int InN = 0;
            // "-fXX"
               if (AV[A][++Ax] != '\0') InN = sscanf(AV[A] + Ax, "%x", &Fill);
            // "-f XX"
               else if (A < AC - 1) InN = sscanf(AV[++A], "%x", &Fill);
               if (InN > 0) Fill &= 0x00ff; // Limit to byte size.
               else { fprintf(stderr, "Error: option -f needs a hexadecimal argument\n"); return 1; }
               Ax = 0; // The end of this arg group.
            }
            break;
         // Parse the program flow.
            case 'l': Listing = true; break;
         // Parse the program flow.
            case 'n': NoAsmF = true; break;
         // The program offset.
            case 'o': {
               int InN = 0;
            // "-oXXXX"
               if (AV[A][++Ax] != '\0') InN = sscanf(AV[A] + Ax, "%x", &BasePC);
            // "-o XXXX"
               else if (A < AC - 1) InN = sscanf(AV[++A], "%x", &BasePC);
               if (InN > 0) BasePC &= 0xffff; // Limit to 64K.
               else { fprintf(stderr, "Error: option -o needs a hexadecimal argument\n"); return 1; }
               Ax = 0; // The end of this arg group.
            }
            break;
            case 'v': Loudness++; break;
            default: Usage(AV[0]); return 1;
         }
      // If one more arg char, keep this arg group.
         if (Ax > 0 && AV[A][Ax + 1]) { A--; continue; }
         Ax = 0; // Start from the beginning in the next arg group.
      } else if (InFile == nullptr) InFile = AV[A];
   // Check the next arg string.
      else { Usage(AV[0]); return 1; }
   if (InFile == nullptr) { Usage(AV[0]); return 1; }
   AsmF = fopen(InFile, "r");
   if (AsmF == nullptr) { fprintf(stderr, "Error: cannot open infile %s\n", InFile); return 1; }
   Log(1, "Processing infile \"%s\"\n", InFile);
   InitSymTab(); // Initialize the symbol table.
   RAM = (uint8_t *)malloc(MaxRAM + 0x100); // Guard against overflow at the RAM top.
   memset(RAM, Fill, MaxRAM); // Erase the 64K RAM.
   CurPC = 0x0000; // The default start address of the code.
   for (LineNo = 1; ; LineNo++) { // For each line:
      uint32_t BegPC = CurPC;
   // Read a single line; exit at the end of the code.
      char *Line = fgets(LineBuf, sizeof LineBuf, AsmF); if (Line == nullptr) break;
   // Remove the end of line marker, tokenize the line, convert it to machine code.
      Line[strlen(Line) - 1] = '\0', TokenizeLine(Line), CompileLine();
   // List, if requested.
      ListOneLine(BegPC, CurPC, Line);
   }
   List("\n");
   fclose(AsmF);
// Cross-reference.
// Iterate over the symbol table.
   for (int S = 0; S < 0x100; S++) for (SymbolP Sym = SymTab[S]; Sym != nullptr; Sym = Sym->Next)
   // Do expressions depend on a symbol?
      if (Sym->Patch != nullptr) printf("----    %s is undefined!\n", Sym->Name);
      else if (Sym->Type == 0) List("%04X%*s\n", Sym->Value, 20 + int(strlen(Sym->Name)), Sym->Name);
   if (LoPC < 0x100 || HiPC <= 0x100) IsCom = false; // Cannot be a CP/M com file.
   if (Listing || Loudness > 0) {
      if (LoPC <= HiPC) printf("\nUsing RAM range [0x%04X...0x%04X]\n", LoPC, HiPC);
      else printf("\nNo data created\n"), exit(1);
   }
   char ExFile[PATH_MAX];
   if (!NoAsmF && strlen(InFile) > 4 && strcmp(InFile + strlen(InFile) - 4, ".asm") == 0) {
      strncpy(ExFile, InFile, sizeof ExFile);
   // Create a out file name(s) from the in file name.
      size_t ExFileN = strlen(ExFile);
   // Make it a bin or com (= bin file that starts at PC = 0x100) file.
      strncpy(ExFile + ExFileN - 3, IsCom? "com": "bin", sizeof ExFile - ExFileN - 3);
      Log(1, "Creating output file %s\n", ExFile);
      BinF = fopen(ExFile, "wb");
      if (BinF == nullptr) { fprintf(stderr, "Error: Can't open output file \"%s\".\n", ExFile); return 1; }
   // A Z80 file is a bin file with a header telling the file offset.
      strncpy(ExFile + ExFileN - 3, "z80", sizeof ExFile - ExFileN - 3);
      Log(1, "Creating output file %s\n", ExFile);
      Z80F = fopen(ExFile, "wb");
      if (Z80F == nullptr) { fprintf(stderr, "Error: Can't open output file \"%s\".\n", ExFile); return 1; }
   // Intel Hex file.
      strncpy(ExFile + ExFileN - 3, "hex", sizeof ExFile - ExFileN - 3);
      Log(1, "Creating output file %s\n", ExFile);
      HexF = fopen(ExFile, "wb");
      if (HexF == nullptr) { fprintf(stderr, "Error: Can't open output file \"%s\".\n", ExFile); return 1; }
   } else Log(1, "No output files created\n"), exit(0);
   if (BinF != nullptr) {
      if (IsCom) BasePC = 0x100;
      fwrite(RAM + BasePC, sizeof RAM[0], HiPC + 1 - BasePC, BinF);
      fclose(BinF);
   }
   if (Z80F != nullptr) PutHeader(Z80F, LoPC), fwrite(RAM + LoPC, sizeof RAM[0], HiPC + 1 - LoPC, Z80F);
   if (HexF != nullptr) {
   {
   // Write the data as Intel Hex.
      HexEx Q; Q.PutAtAddr(LoPC), Q.Put(RAM + LoPC, HiPC + 1 - LoPC);
   }
      fclose(HexF);
   }
   return 0;
}

void CheckPC(uint32_t PC) {
   Log(3, "CheckPC( %04X )", PC);
   if (PC >= MaxRAM) Error("Address overflow -> exit"), exit(0);
   if (PC < LoPC) LoPC = PC;
   if (PC > HiPC) HiPC = PC;
   Log(3, "[%04X..%04X]\n", LoPC, HiPC);
}

void HexEx::Flush(char *Buffer, char *EndP) {
   *EndP = '\0', fputs(Buffer, HexF);
}
