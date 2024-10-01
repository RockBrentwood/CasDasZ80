// Z80 Tokenizer.
#include "Cas.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// clang-format off
struct ShortSym {
   int16_t Id; // ID for the symbol.
   const char *S; // String.
   uint16_t Par; // Additional parameter.
};

static const ShortSym Pseudo[] = {
   { _db, "DEFB", 0x0000 }, { _db, "DB", 0x0000 },
   { _dm, "DEFM", 0x0000 }, { _dm, "DM", 0x0000 },
   { _ds, "DEFS", 0x0000 }, { _ds, "DS", 0x0000 },
   { _dw, "DEFW", 0x0000 }, { _dw, "DW", 0x0000 },
   { _end, "END", 0x0000 },
   { _equ, "EQU", 0x0000 },
   { _org, "ORG", 0x0000 },
   { _if, "IF", 0x0000 },
   { _endif, "ENDIF", 0x0000 },
   { _else, "ELSE", 0x0000 },
   { _print, "PRINT", 0x0000 },
   { _fill, "FILL", 0x0000 }
};

#define Mode1(Op) ((Op) << 8)
#define Mode2(Op1,Op2) (((Op1) << 8) | (Op2))
static const ShortSym Opcodes[] = {
   { _AOp, "ADC", Mode2(0210,0316) }, { _AOp, "ADD", Mode2(0200,0306) }, { _AOp, "AND", Mode2(0240,0346) },
   { _BitOp, "BIT", Mode2(0313,0100) }, { _RefOp, "CALL", Mode2(0304,0315) }, { _UnOp, "CCF", Mode1(0077) },
   { _AOp, "CP", Mode2(0270,0376) }, { _BinOp, "CPD", Mode2(0355,0251) }, { _BinOp, "CPDR", Mode2(0355,0271) },
   { _BinOp, "CPI", Mode2(0355,0241) }, { _BinOp, "CPIR", Mode2(0355,0261) }, { _UnOp, "CPL", Mode1(0057) },
   { _UnOp, "DAA", Mode1(0047) }, { _IOp, "DEC", Mode1(0005) }, { _UnOp, "DI", Mode1(0363) },
   { _djnz, "DJNZ", Mode1(0020) }, { _UnOp, "EI", Mode1(0373) }, { _ex, "EX", Mode2(0343,0353) },
   { _UnOp, "EXX", Mode1(0331) }, { _UnOp, "HALT", Mode1(0166) }, { _im, "IM", Mode2(0355,0106) },
   { _POp, "IN", Mode2(0100,0333) }, { _IOp, "INC", Mode1(0004) }, { _BinOp, "IND", Mode2(0355,0252) },
   { _BinOp, "INDR", Mode2(0355,0272) }, { _BinOp, "INI", Mode2(0355,0242) }, { _BinOp, "INIR", Mode2(0355,0262) },
   { _RefOp, "JP", Mode2(0302,0303) }, { _RefOp, "JR", Mode2(0040,0030) }, { _ld, "LD", Mode1(0000) },
   { _BinOp, "LDD", Mode2(0355,0250) }, { _BinOp, "LDDR", Mode2(0355,0270) }, { _BinOp, "LDI", Mode2(0355,0240) },
   { _BinOp, "LDIR", Mode2(0355,0260) }, { _BinOp, "NEG", Mode2(0355,0104) }, { _UnOp, "NOP", Mode1(0000) },
   { _AOp, "OR", Mode2(0260,0366) }, { _BinOp, "OTDR", Mode2(0355,0273) }, { _BinOp, "OTIR", Mode2(0355,0263) },
   { _POp, "OUT", Mode2(0101,0323) }, { _BinOp, "OUTD", Mode2(0355,0253) }, { _BinOp, "OUTI", Mode2(0355,0243) },
   { _StOp, "POP", Mode2(0301,0341) }, { _StOp, "PUSH", Mode2(0305,0345) }, { _BitOp, "RES", Mode2(0313,0200) },
   { _ret, "RET", Mode2(0300,0311) }, { _BinOp, "RETI", Mode2(0355,0115) }, { _BinOp, "RETN", Mode2(0355,0105) },
   { _ShOp, "RL", Mode2(0020,0026) }, { _UnOp, "RLA", Mode1(0027) }, { _ShOp, "RLC", Mode2(0000,0026) },
   { _UnOp, "RLCA", Mode1(0007) }, { _OpHL, "RLD", Mode2(0355,0157) }, { _ShOp, "RR", Mode2(0030,0036) },
   { _UnOp, "RRA", Mode1(0037) }, { _ShOp, "RRC", Mode2(0010,0016) }, { _UnOp, "RRCA", Mode1(0017) },
   { _OpHL, "RRD", Mode2(0355,0147) }, { _rst, "RST", Mode1(0307) }, { _AOp, "SBC", Mode2(0230,0336) },
   { _UnOp, "SCF", Mode1(0067) }, { _BitOp, "SET", Mode2(0313,0300) }, { _ShOp, "SLA", Mode2(0040,0046) },
   { _ShOp, "SLL", Mode2(0060,0066) }, { _ShOp, "SRA", Mode2(0050,0056) }, { _ShOp, "SRL", Mode2(0070,0076) },
   { _AOp, "SUB", Mode2(0220,0326) }, { _AOp, "XOR", Mode2(0250,0356) }
};

static const ShortSym Register[] = {
   { _A, "A", 0x0000 }, { _AF, "AF", 0x0000 },
   { _B, "B", 0x0000 }, { _BC, "BC", 0x0000 },
   { _C, "C", 0x0000 }, { _D, "D", 0x0000 },
   { _DE, "DE", 0x0000 }, { _E, "E", 0x0000 },
   { _H, "H", 0x0000 }, { _HL, "HL", 0x0000 },
   { _I, "I", 0x0000 }, { _IX, "IX", 0x0000 },
   { _IY, "IY", 0x0000 }, { _L, "L", 0x0000 },
   { _R, "R", 0x0000 }, { _SP, "SP", 0x0000 },
   { _LX, "X", 0x0000 }, { _HX, "HX", 0x0000 },
   { _LY, "Y", 0x0000 }, { _HY, "HY", 0x0000 }
};

static const ShortSym Conditions[] = {
#if 0
   { _cC, "C", 0x0000 }, // Condition C = Register C!
#endif
   { _cM, "M", 0x0000 },
   { _cNC, "NC", 0x0000 }, { _cNZ, "NZ", 0x0000 },
   { _cP, "P", 0x0000 }, { _cPE, "PE", 0x0000 },
   { _cPO, "PO", 0x0000 }, { _cZ, "Z", 0x0000 }
};

struct TokenTable {
   const ShortSym *Table; // Pointer to an opcode list.
   int16_t TableN; // The length of the table in bytes.
};

static const TokenTable Token[] = {
   { Pseudo, sizeof Pseudo/sizeof Pseudo[0] },
   { Opcodes, sizeof Opcodes/sizeof Opcodes[0] },
   { Register, sizeof Register/sizeof Register[0] },
   { Conditions, sizeof Conditions/sizeof Conditions[0] },
   { 0, 0 }
};
// clang-format on

Command CmdBuf[80];	// A tokenized line.
SymbolP SymTab[0x100];	// The symbol table (split by the upper hash byte).

// Calculate a simple hash for a string.
static uint16_t CalcHash(const char *Name) {
   uint16_t Hash = 0;
   for (uint8_t Ch; (Ch = *Name++) != '\0'; ) {
#if 0
      Hash += Ch;
#else
      Hash = (Hash << 4) + Ch;
      uint16_t H = Hash >> 12;
      if (H != 0) Hash ^= H;
#endif
   }
   return Hash;
}

// Search for a symbol, generate one if it didn't already exist.
static SymbolP FindSymbol(const char *Name) {
   uint16_t Hash = CalcHash(Name); // A hash value for the name.
   uint8_t HashB = Hash;
// Search each symbol with a matching hash for a match by name.
   for (SymbolP Sym = SymTab[HashB]; Sym != nullptr; Sym = Sym->Next)
      if (Sym->Hash == Hash && strcmp(Sym->Name, Name) == 0) return Sym;
// Allocate and check clear memory for a new symbol
   SymbolP Sym = (SymbolP)calloc(1, sizeof *Sym); if (Sym == nullptr) return nullptr;
// Link it into the hash list.
   Sym->Next = SymTab[HashB], SymTab[HashB] = Sym;
// Copy the hash and name.
   Sym->Hash = Hash, strcpy(Sym->Name, Name);
   return Sym;
}

// Initialize the symbol table.
void InitSymTab(void) {
// Reset all entries.
   for (int16_t S = 0; S < 0x100; S++) SymTab[S] = nullptr;
// Check all tokens in each token table.
   for (const TokenTable *T = Token; T->Table; T++) for (int16_t n = 0; n < T->TableN; n++) {
   // Add all opcodes to the symbol table
      SymbolP Sym = FindSymbol(T->Table[n].S);
   // Get the ID (≠ 0!) and merge the parameter with it.
      Sym->Type = T->Table[n].Id, Sym->Value = ((int32_t)T->Table[n].Par << 16) | Sym->Type;
   }
}

// Lump the underscore '_' in with alphanumeric characters.
static int IsAlNum(char Ch) { return isalnum(Ch) || Ch == '_'; }

// Tokenize a single line.
void TokenizeLine(char *Line) {
   char *BegLine = Line; // Remember the beginning of the line.
   CommandP Cmd = CmdBuf; // A pointer to the command buffer.
   char UpLine[LineMax];
   for (char *LP = UpLine; (*LP++ = toupper(*Line++)) != '\0'; ); // Convert to capital letters.
   Line = UpLine;
   while (true) { // Parse the whole string.
      char Ch;
      while ((isspace(Ch = *Line++))); // Skip spaces.
      if (Ch == ';' || Ch == '\0') break; // An end-of-line, possibly preceded by a ';' comment, which is skipped.
      char *LP = Line - 1;	// A pointer to the current token.
      Lexical Type = BadL;	// Token class: default: an illegal type.
      int16_t Base = 0;		// Numeric base: binary, octal, decimal or hex.
      bool Dot = false;		// If the token starts with '.'; for pseudo-opcodes.
      bool Dollar = false;	// If the token starts with '$'.
      if (Ch == '.') Ch = *Line++, Dot = true;
      else if (Ch == '$') { // PC or the beginning of a hex numeral.
         if (isalnum(*Line) && *Line <= 'F') Base = 0x10, Ch = *Line++;
         else Dollar = true;
      } else if (strncmp(LP, "0X", 2) == 0 && isxdigit(LP[2])) {
         Line++; // Skip 'X'.
         Ch = *Line++; // First hex digit.
         Base = 0x10;
      }
      long Value;
      if (Dollar) Type = NumL, Value = CurPC;
      else if (IsAlNum(Ch)) { // A…Z, a…z, 0⋯9, _.
      // A buffer for the numeral and a pointer to it, initialized at the beginning.
         char NumBuf[LineMax], *NP = NumBuf;
      // The cumulative highest ASCII character, initialially set to '\0'.
         char MaxCh = '\0';
         do {
            *NP++ = Ch;
            if (IsAlNum(*Line)) { // Not the last character?
               if (Ch > MaxCh) MaxCh = Ch; // Remember the highest ASCII character.
            } else { // The last character.
               if (Base == 0x10) Base = MaxCh <= 'F' && Ch <= 'F'? 0x10: 0; // Invalid hex digits?
               else if (NumBuf + 1 != NP) { // At least one character.
                  if (isdigit(LP[0]) && Ch == 'H' && MaxCh <= 'F') Base = 0x10; // Starts with digit and ends with 'H': hex numeral.
                  else if (Ch == 'D' && MaxCh <= '9') Base = 10; // 'D' after a numeral: decimal numeral.
                  else if ((Ch == 'O' || Ch == 'Q') && MaxCh <= '7') Base = 010; // 'O' or 'Q' after a numeral: octal numeral.
                  else if (Ch == 'B' && MaxCh <= '1') Base = 2; // 'B' after a numeral: binary numeral.
                  if (Base > 0) NP--;
               }
               if (Base == 0 && Ch >= '0' && Ch <= '9' && MaxCh <= '9') Base = 10;
            }
            Ch = *Line++; // Get the next character.
         } while (IsAlNum(Ch));
         Line--;
         *NP = 0;
         if (Base > 0) { // A valid numeral?
            NP = NumBuf, Value = 0;
         // Read the value of the numeral in the given base by multiply-and-add.
            while ((Ch = *NP++) != '\0') Value *= Base, Value += Ch <= '9'? Ch - '0': Ch - 'A' + 10;
            Type = NumL; // Type: a numeral.
         } else {
         // The first character is not a digit or the token doesn't start with "$" or "0X"?
            if (*NumBuf >= 'A' && LP[0] != '$' && strncmp(LP, "0X", 2) != 0) {
            // An opcode or a symbol, or dump out if not retrieved (out of memory).
               SymbolP Sym = FindSymbol(NumBuf); if (Sym == nullptr) break;
               if (Sym->Type == 0) { // Type = symbol?
                  if (Dot) Error("symbols can't start with '.'");
                  Type = SymL, Value = (long)Sym; // Value = address of the symbol pointer.
               // For symbols not yet seen, implicitly define it and unmark it.
                  if (!Sym->First) Sym->First = true, Sym->Defined = false;
               } else {
               // An opcode and parameter, ID.
                  Type = OpL, Value = Sym->Value;
               // Only pseudo opcodes.
                  if (Dot && LexC(Value) != _OpP) Error("opcodes can't start with '.'");
               }
            } else Error("symbols can't start with '$' or digits");
         }
      } else {
         Type = OpL;
         switch (Ch) {
            case '>':
               if (*Line == '>') Value = '}', Line++; // ">>" recognized and punned as '}'.
            break;
            case '<':
               if (*Line == '<') Value = '{', Line++; // "<<" recognized and punned as '{'.
            break;
         // '=' matches EQU
            case '=': Value = _equ; break;
         // An ASCII character with '.'.
            case '\'':
               Value = BegLine[Line - UpLine]; // Not capitalized ASCII character.
               if (Value == 0 || Line[1] != '\'') Value = '\'';
               else {
                  Line++, Type = NumL; // Type: a numeral.
                  if (*Line++ != '\'') break;
               }
            break;
         // An ASCII string with "...".
            case '\"': {
               char *LP = Line;
               int16_t Lx = Line - UpLine; // Offset to the line.
               while (*LP++ != '\"'); // Search for the end of the string.
               LP = (char *)malloc(LP - Line); // Allocate a buffer for the string
               Value = (long)LP;
               if (LP == nullptr) break;
               else {
                  while (*Line++ != '\"') *LP++ = BegLine[Lx++]; // The end of the string found: copy characters.
                  *LP = '\0';
               }
               Type = StrL; // Type: a string.
            }
            break;
            default: Value = Ch;
         }
      }
      Cmd->Type = Type, Cmd->Value = Value; // Copy into the command buffer.
      Cmd++;
   }
   Cmd->Type = BadL, Cmd->Value = 0; // Terminate the command buffer.
}
