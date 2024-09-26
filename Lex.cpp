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
   { _else, "ELSE", 0x0000 }, { _end, "END", 0x0000 },
   { _endif, "ENDIF", 0x0000 }, { _equ, "EQU", 0x0000 }, { _if, "IF", 0x0000 },
   { _org, "ORG", 0x0000 }, { _print, "PRINT", 0x0000 }
};

// Type:
// 0x200 : in,out.
// 0x201 : 1 byte opcode, no parameter.
// 0x202 : 2 byte opcode, no parameter.
// 0x203 : 2 byte opcode, (HL) required.
// 0x204 : first parameter = bit number, second parameter = <ea> (bit,res,set).
// 0x205 : im (one parameter: 0,1,2).
// 0x206 : add,adc,sub,sbc,and,xor,or,cp.
// 0x207 : inc, dec, like 0x206 with absolute address.
// 0x208 : jp, call, jr (Warning! Different <ea>!)
// 0x209 : ret (c or nothing).
// 0x20a : rst (00,08,10,18,20,28,30,38).
// 0x20b : djnz.
// 0x20c : ex: (SP),Rw or DE,HL or AF,AF'.
// 0x20d : ld.
// 0x20e : push, pop: Rw.
// 0x20f : rr,rl,rrc,rlc,sra,sla,srl.
static const ShortSym Opcodes[] = {
   { 0x206, "ADC", 0x88ce }, { 0x206, "ADD", 0x80c6 }, { 0x206, "AND", 0xa0e6 },
   { 0x204, "BIT", 0xcb40 }, { 0x208, "CALL", 0xc4cd }, { 0x201, "CCF", 0x3f00 },
   { 0x206, "CP", 0xb8fe }, { 0x202, "CPD", 0xeda9 }, { 0x202, "CPDR", 0xedb9 },
   { 0x202, "CPI", 0xeda1 }, { 0x202, "CPIR", 0xedb1 }, { 0x201, "CPL", 0x2f00 },
   { 0x201, "DAA", 0x2700 }, { 0x207, "DEC", 0x0500 }, { 0x201, "DI", 0xf300 },
   { 0x20b, "DJNZ", 0x1000 }, { 0x201, "EI", 0xfb00 }, { 0x20c, "EX", 0xe3eb },
   { 0x201, "EXX", 0xd900 }, { 0x201, "HALT", 0x7600 }, { 0x205, "IM", 0xed46 },
   { 0x200, "IN", 0x40db }, { 0x207, "INC", 0x0400 }, { 0x202, "IND", 0xedaa },
   { 0x202, "INDR", 0xedba }, { 0x202, "INI", 0xeda2 }, { 0x202, "INIR", 0xedb2 },
   { 0x208, "JP", 0xc2c3 }, { 0x208, "JR", 0x2018 }, { 0x20d, "LD", 0x0000 },
   { 0x202, "LDD", 0xeda8 }, { 0x202, "LDDR", 0xedb8 }, { 0x202, "LDI", 0xeda0 },
   { 0x202, "LDIR", 0xedb0 }, { 0x202, "NEG", 0xed44 }, { 0x201, "NOP", 0x0000 },
   { 0x206, "OR", 0xb0f6 }, { 0x202, "OTDR", 0xedbb }, { 0x202, "OTIR", 0xedb3 },
   { 0x200, "OUT", 0x41d3 }, { 0x202, "OUTD", 0xedab }, { 0x202, "OUTI", 0xeda3 },
   { 0x20e, "POP", 0xc1e1 }, { 0x20e, "PUSH", 0xc5e5 }, { 0x204, "RES", 0xcb80 },
   { 0x209, "RET", 0xc0c9 }, { 0x202, "RETI", 0xed4d }, { 0x202, "RETN", 0xed45 },
   { 0x20f, "RL", 0x1016 }, { 0x201, "RLA", 0x1700 }, { 0x20f, "RLC", 0x0016 },
   { 0x201, "RLCA", 0x0700 }, { 0x203, "RLD", 0xed6f }, { 0x20f, "RR", 0x181e },
   { 0x201, "RRA", 0x1f00 }, { 0x20f, "RRC", 0x080e }, { 0x201, "RRCA", 0x0f00 },
   { 0x203, "RRD", 0xed67 }, { 0x20a, "RST", 0xc700 }, { 0x206, "SBC", 0x98de },
   { 0x201, "SCF", 0x3700 }, { 0x204, "SET", 0xcbc0 }, { 0x20f, "SLA", 0x2026 },
   { 0x20f, "SLL", 0x3036 }, { 0x20f, "SRA", 0x282e }, { 0x20f, "SRL", 0x383e },
   { 0x206, "SUB", 0x90d6 }, { 0x206, "XOR", 0xa8ee }
};

static const ShortSym Register[] = {
   { 0x307, "A", 0x0000 }, { 0x323, "AF", 0x0000 }, // 00…07: B,C,D,E,H,L,(HL),A
   { 0x300, "B", 0x0000 }, { 0x310, "BC", 0x0000 }, // 10…13: BC,DE,HL,SP
   { 0x301, "C", 0x0000 }, { 0x302, "D", 0x0000 },  //    23:         ,AF
   { 0x311, "DE", 0x0000 }, { 0x303, "E", 0x0000 }, // 30…31: IX,IY
   { 0x304, "H", 0x0000 }, { 0x312, "HL", 0x0000 }, // 40…41: R,I
   { 0x341, "I", 0x0000 }, { 0x330, "IX", 0x0000 }, // 54…55: X,HX
   { 0x331, "IY", 0x0000 }, { 0x305, "L", 0x0000 }, // 64…65: Y,HY
   { 0x340, "R", 0x0000 }, { 0x313, "SP", 0x0000 },
   { 0x355, "X", 0x0000 }, { 0x354, "HX", 0x0000 },
   { 0x365, "Y", 0x0000 }, { 0x364, "HY", 0x0000 }
};

static const ShortSym Conditions[] = {
#if 0
   { 0x403, "C", 0x0000 }, // Condition C = Register C!
#endif
   { 0x407, "M", 0x0000 },
   { 0x402, "NC", 0x0000 }, { 0x400, "NZ", 0x0000 },
   { 0x406, "P", 0x0000 }, { 0x405, "PE", 0x0000 },
   { 0x404, "PO", 0x0000 }, { 0x401, "Z", 0x0000 }
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
                  if (Dot && (Value < 0x100 || Value >= 0x200)) Error("opcodes can't start with '.'");
               }
            } else Error("symbols can't start with '$' or digits");
         }
      } else {
         Type = OpL;
         switch (Ch) {
            case '>':
               if (*Line == '>') Value = 0x120, Line++; // '>>' recognized.
            break;
            case '<':
               if (*Line == '<') Value = 0x121, Line++; // '<<' recognized.
            break;
         // '=' matches EQU
            case '=': Value = 0x105; break;
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
      if (Loudness >= 3) switch (Type) {
         case BadL: Log(3, "BadL\n"); break;
         case NumL: Log(3, "NumL:    %lX\n", Value); break;
         case OpL:
            if (Value < 0x100)
               Log(3, "OpL: '%c'\n", Value);
            else
               Log(3, "OpL: %lX\n", Value);
         break;
         case SymL: Log(3, "SymL: %s\n", Value); break;
         case StrL: Log(3, "StrL: \"%s\"\n", (char *)Value); break;
      }
   }
   Cmd->Type = BadL, Cmd->Value = 0; // Terminate the command buffer.
}
