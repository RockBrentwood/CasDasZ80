// Z80 Tokenizer
#include "Cas.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// clang-format off
struct ShortSym {
   int16_t Id; // ID for the symbol
   const char *S; // string
   uint16_t Par; // additional parameter
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
// 0x200 : IN,OUT
// 0x201 : 1 byte opcode, no parameter
// 0x202 : 2 byte opcode, no parameter
// 0x203 : 2 byte opcode, (HL) required
// 0x204 : 1.parameter = bit number, 2.parameter = <ea> (BIT,RES,SET)
// 0x205 : IM (one parameter: 0,1,2)
// 0x206 : ADD,ADC,SUB,SBC,AND,XOR,OR,CP
// 0x207 : INC, DEC, like 0x206 with absolute address
// 0x208 : JP, CALL, JR (Warning! Different <ea>!)
// 0x209 : RET (c or nothing)
// 0x20a : RST (00,08,10,18,20,28,30,38)
// 0x20b : DJNZ
// 0x20c : EX: (SP),dreg or DE,HL or AF,AF'
// 0x20d : LD
// 0x20e : PUSH, POP: dreg
// 0x20f : RR,RL,RRC,RLC,SRA,SLA,SRL
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
   const ShortSym *Table; // ptr to an opcode list
   int16_t TableN; // length of the table in bytes
};

static const TokenTable Token[] = {
   { Pseudo, sizeof Pseudo/sizeof(ShortSym) },
   { Opcodes, sizeof Opcodes/sizeof(ShortSym) },
   { Register, sizeof Register/sizeof(ShortSym) },
   { Conditions, sizeof Conditions/sizeof(ShortSym) },
   { 0, 0 }
};
// clang-format on

Command CmdBuf[80]; // a tokenized line
SymbolP SymTab[0x100]; // symbol table (split by the upper hash byte)

// calculate a simple hash for a string
static uint16_t CalcHash(const char *Name) {
   uint16_t Hash = 0;
   for (uint8_t Ch; (Ch = *Name++) != 0; ) {
#if 0
      Hash += Ch;
#else
      Hash = (Hash << 4) + Ch;
      uint16_t H = (Hash >> 12);
      if (H != 0)
         Hash ^= H;
#endif
   }
   return Hash;
}

// search for a symbol, generate one if it didn't already exist.
static SymbolP FindSymbol(const char *Name) {
   uint16_t Hash = CalcHash(Name); // hash value for the name
   uint8_t HashB = Hash;
   for (SymbolP Sym = SymTab[HashB]; Sym; Sym = Sym->Next) { // For each symbol
      if (Sym->Hash == Hash) // search for a matching hash
         if (!strcmp(Sym->Name, Name))
            return Sym; // found the symbol?
   }
   SymbolP Sym = (SymbolP)malloc(sizeof(Symbol)); // allocate memory for a symbol
   if (!Sym)
      return nullptr; // not enough memory
   memset(Sym, 0, sizeof(Symbol));
   Sym->Next = SymTab[HashB];
   SymTab[HashB] = Sym; // link the symbol into the list
   Sym->Hash = Hash;
   strcpy(Sym->Name, Name); // and copy the name
   return Sym;
}

// initialize the symbol table
void InitSymTab(void) {
   for (int16_t S = 0; S < 0x100; S++)
      SymTab[S] = nullptr; // reset all entries
   for (const TokenTable *T = Token; T->Table; T++) { // check all token tables
      for (int16_t n = 0; n < T->TableN; n++) { // and all tokens for a single table
         SymbolP Sym = FindSymbol(T->Table[n].S); // add all opcodes to the symbol table
         Sym->Type = T->Table[n].Id; // ID (<> 0!)
         Sym->Value = ((int32_t)T->Table[n].Par << 16) | Sym->Type; // merge parameter and id
      }
   }
}

// Is this an alphanumeric character _or_ an unterline, which is a valid symbol
static int IsAlNum(char Ch) {
   return isalnum(Ch) || Ch == '_';
}

// tokenize a single line
void TokenizeLine(char *Line) {
   char *BegLine = Line; // remember the beginning of the line
   CommandP Cmd = CmdBuf; // ptr to the command buffer
   char UpLine[LineMax];
   for (char *LP = UpLine; (*LP++ = toupper(*Line++)); ); // convert to capital letters
   Line = UpLine;
   while (1) { // parse the whole string
      char Ch;
      while ((isspace(Ch = *Line++))); // ignore spaces
      if (Ch == ';')
         break; // a comment => ignore the rest of the line
      if (Ch == 0)
         break; // end of line => done
      char *LP = Line - 1; // pointer to current token
      Lexical Type = BadL; // default: an illegal type
      int16_t Base = 0; // binary, decimal or hex
      bool Dot = false; // pseudo opcodes can start with '.'
      bool Dollar = false; // token starts with $ = PC
      if (Ch == '.') {
         Ch = *Line++;
         Dot = true;
      } else if (Ch == '$') { // PC or the beginning of a hex number
         if (isalnum(*Line) && *Line <= 'F') {
            Base = 0x10;
            Ch = *Line++;
         } else
            Dollar = true;
      } else if (!strncmp(LP, "0X", 2) && isxdigit(LP[2])) {
         Line++; // skip 'X'
         Ch = *Line++; // 1st hex digit
         Base = 0x10;
      }
      long Value;
      if (Dollar) {
         Type = NumL;
         Value = CurPC;
      } else if (IsAlNum(Ch)) { // A…Z, a…z, 0⋯9, _
         char NumBuf[LineMax];
         char *NP = NumBuf; // ptr to the beginning
         char MaxCh = 0; // highest ASCII character
         do {
            *NP++ = Ch;
            if (IsAlNum(*Line)) { // not the last character?
               if (Ch > MaxCh)
                  MaxCh = Ch; // remember the highest ASCII character
            } else { // last character
               if (Base == 0x10) {
                  Base = (MaxCh <= 'F' && Ch <= 'F')? 0x10: 0; // invalid hex digits?
               } else if (NumBuf + 1 != NP) { // at least one character
                  if (isdigit(LP[0]) && Ch == 'H' && MaxCh <= 'F')
                     Base = 0x10; // starts with digit and ends with 'H': hex number
                  else if (Ch == 'D' && MaxCh <= '9')
                     Base = 10; // 'D' after a number: decimal number
                  else if (Ch == 'B' && MaxCh <= '1')
                     Base = 2; // 'B' after a number: binary number
                  if (Base > 0)
                     --NP;
               }
               if (!Base && Ch >= '0' && Ch <= '9' && MaxCh <= '9')
                  Base = 10;
            }
            Ch = *Line++; // get the next character
         } while (IsAlNum(Ch));
         Line--;
         *NP = 0;
         if (Base > 0) { // a valid number?
            NP = NumBuf;
            Value = 0;
            while ((Ch = *NP++) != 0) { // read the value
               Value *= Base; // multiply with the number base
               Value += (Ch <= '9')? Ch - '0': Ch - 'A' + 10;
            }
            Type = NumL; // Type: a number
         } else {
         // first character not a digit or token doesn't start with "$" or "0X"?
            if (*NumBuf >= 'A' && LP[0] != '$' && strncmp(LP, "0X", 2)) {
               SymbolP Sym = FindSymbol(NumBuf); // an opcode or a symbol
               if (!Sym)
                  break; // error (out of memory)
               if (!Sym->Type) { // Type = symbol?
                  if (Dot)
                     Error("symbols can't start with '.'");
                  Type = SymL;
                  Value = (long)Sym; // value = address of the symbol ptr
                  if (!Sym->First) { // symbol already exists?
                     Sym->First = true; // no, then implicitly define it
                     Sym->Defined = false; // symbol value not defined
                  }
               } else {
                  Type = OpL; // an opcode
                  Value = Sym->Value; // parameter, ID
                  if (Dot && (Value < 0x100 || Value >= 0x200)) // only pseudo opcodes
                     Error("opcodes can't start with '.'");
               }
            } else
               Error("symbols can't start with '$' or digits");
         }
      } else {
         Type = OpL;
         switch (Ch) {
            case '>':
               if (*Line == '>') {
                  Value = 0x120; // >> recognized
                  Line++;
               }
            break;
            case '<':
               if (*Line == '<') {
                  Value = 0x121; // << recognized
                  Line++;
               }
            break;
            case '=':
               Value = 0x105; // = matches EQU
            break;
            case '\'': // an ASCII character with '.'
               Value = BegLine[Line - UpLine]; // not capitalized ASCII character
               if ((!Value) || (Line[1] != '\'')) {
                  Value = '\'';
               } else {
                  Line++;
                  Type = NumL; // Type: a number
                  if (*Line++ != '\'')
                     break;
               }
            break;
            case '\"': { // an ASCII string with "..."
               char *LP = Line;
               int16_t Lx = Line - UpLine; // offset to the line
               while (*LP++ != '\"'); // search for the end of the string
               LP = (char *)malloc(LP - Line); // allocate a buffer for the string
               Value = (long)LP;
               if (!LP)
                  break;
               else {
                  while (*Line++ != '\"') // end of the string found?
                     *LP++ = BegLine[Lx++]; // copy characters
                  *LP = 0;
               }
               Type = StrL; // Type: a string
            }
            break;
            default:
               Value = Ch;
         }
      }
      Cmd->Type = Type;
      Cmd->Value = Value; // copy into the command buffer
      Cmd++;
      if (Loudness >= 3)
         switch (Type) {
            case BadL:
               Log(3, "BadL\n");
            break;
            case NumL:
               Log(3, "NumL:    %lX\n", Value);
            break;
            case OpL:
               if (Value < 0x100)
                  Log(3, "OpL: '%c'\n", Value);
               else
                  Log(3, "OpL: %lX\n", Value);
            break;
            case SymL:
               Log(3, "SymL: %s\n", Value);
            break;
            case StrL:
               Log(3, "StrL: \"%s\"\n", (char *)Value);
            break;
         }
   }
   Cmd->Type = BadL;
   Cmd->Value = 0; // terminate the command buffer
}
