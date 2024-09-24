// Z80 Assembler
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include "HexEx.h"
#include "Cas.h"

uint32_t CurPC; // current address
uint8_t *RAM; // 64K RAM of the Z80
static const uint32_t MaxRAM = 0x10000;
static uint32_t LoPC = MaxRAM;
static uint32_t HiPC = 0;
static bool Listing = false;
static FILE *AsmF;
static FILE *BinF;
static FILE *Z80F;
static FILE *HexF;
int Loudness = 0;
static long LineNo; // current line number
static char LineBuf[LineMax]; // buffer for the current line

// print a fatal error message and exit
void Error(const char *Message) {
   printf("Error in line %ld: %s\n", LineNo, Message);
   const char *p;
   for (p = LineBuf; isspace(*p); p++);
   puts(p);
   exit(1);
}

static void Usage(const char *Path) {
   const char *App = nullptr;
   for (char Ch; (Ch = *Path++); )
      if (Ch == '/' || Ch == '\\')
         App = Path;
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

// create listing for one sorce code line
// address    data bytes    source code
// break long data block (e.g. defm) into lines of 4 data bytes
static void ListOneLine(uint32_t BegPC, uint32_t EndPC, const char *Line) {
   if (!Listing)
      return;
   if (BegPC == EndPC) {
      printf("%*s\n", 24 + int(strlen(Line)), Line);
   } else {
      printf("%4.4X   ", BegPC);
      uint32_t PC = BegPC;
      int n = 0;
      while (PC < EndPC) {
         printf(" %2.2X", RAM[PC++]);
         if (n == 3)
            printf("     %s", Line);
         if ((n&3) == 3) {
            printf("\n");
            if (PC < EndPC)
               printf("%4.4X   ", PC);
         }
         ++n;
      }
      if (n < 4)
         printf("%*s\n", 5 + 3*(4 - n) + int(strlen(Line)), Line);
      else if ((n&3))
         printf("\n");
   }
}

void Log(int Loud, const char *Format, ...) {
   if (Loudness >= Loud) {
      while (Loud--)
         fprintf(stderr, " ");
      va_list AP;
      va_start(AP, Format);
      vfprintf(stderr, Format, AP);
      va_end(AP);
   }
}

void List(const char *Format, ...) {
   if (Listing) {
      va_list AP;
      va_start(AP, Format);
      vprintf(Format, AP);
      va_end(AP);
   }
}

// the z80 format is used by the z80-asm
// http://wwwhomes.uni-bielefeld.de/achim/z80-asm.html
// *.z80 files are bin files with a header telling the bin offset
//	struct z80_header {
//		const char *Signature = "Z80ASM" "\032" "\n";
//		uint16_t Offset;
//	}
static void PutHeader(FILE *ExF, uint32_t Addr) {
   const char *Signature = "Z80ASM" "\032" "\n";
   unsigned char Buf[2];
   Buf[0] = Addr&0xff;
   Buf[1] = Addr >> 8;
   fwrite(Signature, 1, strlen(Signature), ExF);
   fwrite(Buf, 1, 2, ExF);
}

// …
int main(int AC, char **AV) {
   char *InFile = nullptr;
   int ComPC = 0;
   int BasePC = 0;
   int Fill = 0;
   bool NoAsmF = false;
   fprintf(stderr, "CasZ80 - a small 1-pass assembler for Z80 code\n");
   fprintf(stderr, "Based on TurboAss Z80 (c)1992-1993 Sigma-Soft, Markus Fritze\n");
   for (int A = 1, Ax = 0; A < AC; A++) {
      if ('-' == AV[A][0]) {
         switch (AV[A][++Ax]) {
            case 'c': // create cp/m com file
               ComPC = 0x100;
            break;
            case 'f': { // fill
               int InN = 0;
               if (AV[A][++Ax]) // "-fXX"
                  InN = sscanf(AV[A] + Ax, "%x", &Fill);
               else if (A < AC - 1) // "-f XX"
                  InN = sscanf(AV[++A], "%x", &Fill);
               if (InN)
                  Fill &= 0x00ff; // limit to byte size
               else {
                  fprintf(stderr, "Error: option -f needs a hexadecimal argument\n");
                  return 1;
               }
               Ax = 0; // end of this arg group
            }
            break;
            case 'l': // parse program flow
               Listing = true;
            break;
            case 'n': // parse program flow
               NoAsmF = true;
            break;
            case 'o': { // program offset
               int InN = 0;
               if (AV[A][++Ax]) // "-oXXXX"
                  InN = sscanf(AV[A] + Ax, "%x", &BasePC);
               else if (A < AC - 1) // "-o XXXX"
                  InN = sscanf(AV[++A], "%x", &BasePC);
               if (InN)
                  BasePC &= 0xffff; // limit to 64K
               else {
                  fprintf(stderr, "Error: option -o needs a hexadecimal argument\n");
                  return 1;
               }
               Ax = 0; // end of this arg group
            }
            break;
            case 'v':
               ++Loudness;
            break;
            default:
               Usage(AV[0]);
               return 1;
         }
         if (Ax && AV[A][Ax + 1]) { // one more arg char
            --A; // keep this arg group
            continue;
         }
         Ax = 0; // start from the beginning in next arg group
      } else {
         if (!InFile)
            InFile = AV[A];
         else {
            Usage(AV[0]);
            return 1;
         } // check next arg string
      }
   }
   if (!InFile) {
      Usage(AV[0]);
      return 1;
   }
   AsmF = fopen(InFile, "r");
   if (!AsmF) {
      fprintf(stderr, "Error: cannot open infile %s\n", InFile);
      return 1;
   }
   Log(1, "Processing infile \"%s\"\n", InFile);
   LineNo = 1;
   InitSymTab(); // init symbol table
   RAM = (uint8_t *)malloc(MaxRAM + 0x100); // guard against overflow at ram top
   memset(RAM, Fill, MaxRAM); // erase 64K RAM
   CurPC = 0x0000; // default start address of the code
   while (true) {
      uint32_t BegPC = CurPC;
      char *Line = fgets(LineBuf, sizeof(LineBuf), AsmF); // read a single line
      if (!Line)
         break; // end of the code => exit
      *(Line + strlen(Line) - 1) = 0; // remove end of line marker
      TokenizeLine(Line); // tokenize line
      CompileLine(); // generate machine code for the line
      ListOneLine(BegPC, CurPC, Line); // create listing if enabled
      LineNo++; // next line
   }
   List("\n");
   fclose(AsmF);
// cross reference
   for (int S = 0; S < 0x100; S++) { // iterate over symbol table
      for (SymbolP Sym = SymTab[S]; Sym; Sym = Sym->Next)
         if (Sym->Patch) // depend expressions on a symbol?
            printf("----    %s is undefined!\n", Sym->Name);
         else if (!Sym->Type)
            List("%04X%*s\n", Sym->Value, 20 + int(strlen(Sym->Name)), Sym->Name);
   }
   if (LoPC < 0x100 || HiPC <= 0x100) // cannot be a CP/M com file
      ComPC = 0;
   if (Listing || Loudness) {
      if (LoPC <= HiPC)
         printf("\nUsing RAM range [0x%04X...0x%04X]\n", LoPC, HiPC);
      else {
         printf("\nNo data created\n");
         exit(1);
      }
   }
   char ExFile[PATH_MAX];
   if (!NoAsmF && strlen(InFile) > 4 && !strcmp(InFile + strlen(InFile) - 4, ".asm")) {
      strncpy(ExFile, InFile, sizeof(ExFile));
   // create out file name(s) from in file name
      size_t ExFileN = strlen(ExFile);
   // bin or com (=bin file that starts at PC=0x100) file
      strncpy(ExFile + ExFileN - 3, ComPC? "com": "bin", sizeof(ExFile) - ExFileN - 3);
      Log(1, "Creating output file %s\n", ExFile);
      BinF = fopen(ExFile, "wb");
      if (!BinF) {
         fprintf(stderr, "Error: Can't open output file \"%s\".\n", ExFile);
         return 1;
      }
   // z80 file is a bin file with a header telling the file offset
      strncpy(ExFile + ExFileN - 3, "z80", sizeof(ExFile) - ExFileN - 3);
      Log(1, "Creating output file %s\n", ExFile);
      Z80F = fopen(ExFile, "wb");
      if (!Z80F) {
         fprintf(stderr, "Error: Can't open output file \"%s\".\n", ExFile);
         return 1;
      }
   // intel hex file
      strncpy(ExFile + ExFileN - 3, "hex", sizeof(ExFile) - ExFileN - 3);
      Log(1, "Creating output file %s\n", ExFile);
      HexF = fopen(ExFile, "wb");
      if (!HexF) {
         fprintf(stderr, "Error: Can't open output file \"%s\".\n", ExFile);
         return 1;
      }
   } else {
      Log(1, "No output files created\n");
      exit(0);
   }
   if (BinF) {
      if (ComPC)
         fwrite(RAM + 0x100, sizeof(uint8_t), HiPC + 1 - 0x100, BinF);
      else
         fwrite(RAM + BasePC, sizeof(uint8_t), HiPC + 1 - BasePC, BinF);
      fclose(BinF);
   }
   if (Z80F) {
      PutHeader(Z80F, LoPC);
      fwrite(RAM + LoPC, sizeof(uint8_t), HiPC + 1 - LoPC, Z80F);
   }
   if (HexF) {
   // write the data as intel hex
      struct HexQ Qb;
      HexExBeg(&Qb);
      HexPutAtAddr(&Qb, LoPC);
      HexPut(&Qb, RAM + LoPC, HiPC + 1 - LoPC);
      HexExEnd(&Qb);
      fclose(HexF);
   }
   return 0;
}

void CheckPC(uint32_t PC) {
   Log(3, "CheckPC( %04X )", PC);
   if (PC >= MaxRAM) {
      Error("Address overflow -> exit");
      exit(0);
   }
   if (PC < LoPC)
      LoPC = PC;
   if (PC > HiPC)
      HiPC = PC;
   Log(3, "[%04X..%04X]\n", LoPC, HiPC);
}

void HexExFlush(struct HexQ *Qh, char *Buf, char *EndP) {
   (void)Qh;
   *EndP = '\0';
   (void)fputs(Buf, HexF);
}
