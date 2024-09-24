// Z80 Tokenizer
#include "Cas.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

uint16_t CalcHash(const char *name);
SymbolP FindSymbol(const char *name);
void InitSymTab(void);

// clang-format off
typedef struct {
   int16_t id; // ID for the symbol
   const char *s; // string
   uint16_t p; // additional parameter
} ShortSym;

static const ShortSym Pseudo[] = {
   { DEFB, "DEFB", 0x0000 }, { DEFB, "DB", 0x0000 },
   { DEFM, "DEFM", 0x0000 }, { DEFM, "DM", 0x0000 },
   { DEFS, "DEFS", 0x0000 }, { DEFS, "DS", 0x0000 },
   { DEFW, "DEFW", 0x0000 }, { DEFW, "DW", 0x0000 },
   { ELSE, "ELSE", 0x0000 }, { END, "END", 0x0000 },
   { ENDIF, "ENDIF", 0x0000 }, { EQU, "EQU", 0x0000 }, { IF, "IF", 0x0000 },
   { ORG, "ORG", 0x0000 }, { PRINT, "PRINT", 0x0000 }
};

// type: (+ 0x200)
// 0x00 : IN,OUT
// 0x01 : 1 byte opcode, no parameter
// 0x02 : 2 byte opcode, no parameter
// 0x03 : 2 byte opcode, (HL) required
// 0x04 : 1.parameter = bit number, 2.parameter = <ea> (BIT,RES,SET)
// 0x05 : IM (one parameter: 0,1,2)
// 0x06 : ADD,ADC,SUB,SBC,AND,XOR,OR,CP
// 0x07 : INC, DEC, like 0x06 with absolute address
// 0x08 : JP, CALL, JR (Warning! Different <ea>!)
// 0x09 : RET (c or nothing)
// 0x0a : RST (00,08,10,18,20,28,30,38)
// 0x0b : DJNZ
// 0x0c : EX: (SP),dreg or DE,HL or AF,AF'
// 0x0d : LD
// 0x0e : PUSH, POP: dreg
// 0x0f : RR,RL,RRC,RLC,SRA,SLA,SRL
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

typedef struct {
   const ShortSym *table; // ptr to an opcode list
   int16_t tablesize; // length of the table in bytes
} TokenTable;

static const TokenTable Token[] = {
   { Pseudo, sizeof Pseudo/sizeof(ShortSym) },
   { Opcodes, sizeof Opcodes/sizeof(ShortSym) },
   { Register, sizeof Register/sizeof(ShortSym) },
   { Conditions, sizeof Conditions/sizeof(ShortSym) },
   { 0, 0 }
};
// clang-format on

Command Cmd[80]; // a tokenized line
SymbolP SymTab[256]; // symbol table (split with the hash byte)

// calculate a simple hash for a string
uint16_t CalcHash(const char *name) {
   uint16_t hash_val = 0;
   uint16_t i;
   uint8_t c;
   while ((c = *name++) != 0) {
#if 0
      hash_val += c;
#else
      hash_val = (hash_val << 4) + c;
      if ((i = (hash_val >> 12)) != 0)
         hash_val ^= i;
#endif
   }
   return hash_val;
}

// search for a symbol, generate one if it didn't already exist.
SymbolP FindSymbol(const char *name) {
   uint16_t hash = CalcHash(name); // hash value for the name
   uint8_t hashb = hash;
   SymbolP s;
   s = SymTab[hashb]; // ptr to the first symbol
   while (s) {
      if (s->hash == hash) // search for a matching hash
         if (!strcmp(s->name, name))
            return s; // found the symbol?
      s = s->next; // to the next symbol
   }
   s = (SymbolP)malloc(sizeof(Symbol)); // allocate memory for a symbol
   if (!s)
      return nullptr; // not enough memory
   memset(s, 0, sizeof(Symbol));
   s->next = SymTab[hashb];
   SymTab[hashb] = s; // link the symbol into the list
   s->hash = hash;
   strcpy(s->name, name); // and copy the name
   return s;
}

// initialize the symbol table
void InitSymTab(void) {
   int16_t i;
   SymbolP s;
   const TokenTable *t;
   for (i = 0; i < 256; i++)
      SymTab[i] = nullptr; // reset all entries
   for (t = Token; t->table; t++) { // check all token tables
      for (i = 0; i < t->tablesize; i++) { // and all tokens for a single table
         s = FindSymbol(t->table[i].s); // add all opcodes to the symbol table
         s->type = t->table[i].id; // ID (<> 0!)
         s->val = ((int32_t)t->table[i].p << 16) | s->type; // merge parameter and id
      }
   }
}

// Is this an alphanumeric character _or_ an unterline, which is a valid symbol
int isalnum_(char c) {
   return isalnum(c) || c == '_';
}

// tokenize a single line
void TokenizeLine(char *sp) {
   char *tp, *sp2;
   char c;
   char stemp[MAXLINELENGTH];
   char maxc;
   int16_t base; // binary, decimal or hex
   bool dollar; // token starts with $
   bool dot;
   Type typ;
   long val;
   char AktUpLine[MAXLINELENGTH];
   char *AktLine = sp; // remember the beginning of the line
   CommandP cp = Cmd; // ptr to the command buffer
   sp2 = AktUpLine;
   while ((*sp2++ = toupper(*sp++))); // convert to capital letters
   sp = AktUpLine;
   while (1) { // parse the whole string
      while ((isspace(c = *sp++))); // ignore spaces
      if (c == ';')
         break; // a comment => ignore the rest of the line
      if (c == 0)
         break; // end of line => done
      tp = sp - 1; // pointer to current token
      typ = ILLEGAL; // default: an illegal type
      base = 0;
      dot = false; // pseudo opcodes can start with '.'
      dollar = false; // $ = PC
      if (c == '.') {
         c = *sp++;
         dot = true;
      } else if (c == '$') { // PC or the beginning of a hex number
         if (isalnum(*sp) && *sp <= 'F') {
            base = 16;
            c = *sp++;
         } else
            dollar = true;
      } else if (!strncmp(tp, "0X", 2) && isxdigit(tp[2])) {
         sp++; // skip 'X'
         c = *sp++; // 1st hex digit
         base = 16;
      }
      if (dollar) {
         typ = NUM;
         val = PC;
      } else if (isalnum_(c)) { // A…Z, a…z, 0-9
         sp2 = stemp; // ptr to the beginning
         maxc = 0; // highest ASCII character
         do {
            *sp2++ = c;
            if (isalnum_(*sp)) { // not the last character?
               if (c > maxc)
                  maxc = c; // remember the highest ASCII character
            } else { // last character
               if (base == 16) {
                  base = (maxc <= 'F' && c <= 'F')? 16: 0; // invalid hex digits?
               } else if (stemp + 1 != sp2) { // at least one character
                  if (isdigit(tp[0]) && c == 'H' && maxc <= 'F')
                     base = 16; // starts with digit and ends with 'H': hex number
                  else if (c == 'D' && maxc <= '9')
                     base = 10; // 'D' after a number: decimal number
                  else if (c == 'B' && maxc <= '1')
                     base = 2; // 'B' after a number: binary number
                  if (base > 0)
                     --sp2;
               }
               if (!base && c >= '0' && c <= '9' && maxc <= '9')
                  base = 10;
            }
            c = *sp++; // get the next character
         } while (isalnum_(c));
         sp--;
         *sp2 = 0;
         if (base > 0) { // a valid number?
            sp2 = stemp;
            val = 0;
            while ((c = *sp2++) != 0) { // read the value
               val *= base; // multiply with the number base
               val += (c <= '9')? c - '0': c - 'A' + 10;
            }
            typ = NUM; // type: a number
         } else {
         // first character not a digit or token doesn't start with "$" or "0X"?
            if (*stemp >= 'A' && tp[0] != '$' && strncmp(tp, "0X", 2)) {
               SymbolP sym = FindSymbol(stemp); // an opcode or a symbol
               if (!sym)
                  break; // error (out of memory)
               if (!sym->type) { // typ = symbol?
                  if (dot)
                     Error("symbols can't start with '.'");
                  typ = SYMBOL;
                  val = (long)sym; // value = address of the symbol ptr
                  if (!sym->first) { // symbol already exists?
                     sym->first = true; // no, then implicitly define it
                     sym->defined = false; // symbol value not defined
                  }
               } else {
                  typ = OPCODE; // an opcode
                  val = sym->val; // parameter, ID
                  if (dot && (val < 0x100 || val >= 0x200)) // only pseudo opcodes
                     Error("opcodes can't start with '.'");
               }
            } else
               Error("symbols can't start with '$' or digits");
         }
      } else {
         typ = OPCODE;
         switch (c) {
            case '>':
               if (*sp == '>') {
                  val = 0x120; // >> recognized
                  sp++;
               }
            break;
            case '<':
               if (*sp == '<') {
                  val = 0x121; // << recognized
                  sp++;
               }
            break;
            case '=':
               val = 0x105; // = matches EQU
            break;
            case '\'': // an ASCII character with '.'
               val = AktLine[sp - AktUpLine]; // not capitalized ASCII character
               if ((!val) || (sp[1] != '\'')) {
                  val = '\'';
               } else {
                  sp++;
                  typ = NUM; // typ: a number
                  if (*sp++ != '\'')
                     break;
               }
            break;
            case '\"': // an ASCII string with "..."
               sp2 = sp;
               base = sp - AktUpLine; // offset to the line
               while (*sp2++ != '\"'); // search for the end of the string
               sp2 = (char *)malloc(sp2 - sp); // allocate a buffer for the string
               val = (long)sp2;
               if (!sp2)
                  break;
               else {
                  while (*sp++ != '\"') // end of the string found?
                     *sp2++ = AktLine[base++]; // copy characters
                  *sp2 = 0;
               }
               typ = STRING; // type: a string
            break;
            default:
               val = c;
         }
      }
      cp->typ = typ;
      cp->val = val; // copy into the command buffer
      cp++;
      if (verboseMode >= 3)
         switch (typ) {
            case ILLEGAL:
               MSG(3, "ILLEGAL\n");
            break;
            case NUM:
               MSG(3, "NUM:    %lX\n", val);
            break;
            case OPCODE:
               if (val < 0x100)
                  MSG(3, "OPCODE: '%c'\n", val);
               else
                  MSG(3, "OPCODE: %lX\n", val);
            break;
            case SYMBOL:
               MSG(3, "SYMBOL: %s\n", val);
            break;
            case STRING:
               MSG(3, "STRING: \"%s\"\n", (char *)val);
            break;
         }
   }
   cp->typ = ILLEGAL;
   cp->val = 0; // terminate the command buffer
}
