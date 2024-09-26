#define LineMax 0x100
#define NameMax 32
#define DEBUG 0

#include <cstdint>

enum Lexical {
   BadL,
   NumL,	// A numeral.
   OpL,		// An ASCII literal (0x00…0xff) or opcode (≥ 0x100). See below.
   SymL,	// A symbol.
   StrL		// A string.
};

enum Pseudo_t { _db = 0x100, _dm, _ds, _dw, _end, _equ, _org, _if, _endif, _else, _print, _fill };

// An encoded opcode.
typedef struct Command {
   Lexical Type;
   long Value;
} *CommandP;

// Expressions for back-patching.
typedef struct PatchList *PatchListP;
struct PatchList {
   PatchListP Next;	// The next entry in the list.
   uint16_t Type;	// The expression's patched type (0: 1 byte, 1: 2 bytes (lo/hi); 2: PC-relative to Addr + 1).
   uint32_t Addr;	// The patched address.
   CommandP Cmd;	// A pointer to the expression.
};

// A symbol table entry.
typedef struct Symbol *SymbolP;
struct Symbol {
   SymbolP Next;		// The next symbol in the list.
   uint16_t Hash;		// The symbol name's hash value.
   uint16_t Type;		// Type: 0 = symbol; <>0 = opcode, etc.
   char Name[NameMax + 1];	// The symbol's name.
   int32_t Value;		// The symbol's value.
   unsigned Defined:1;		// True, if the symbol is defined.
   unsigned First:1;		// True, if the symbol is already valid.
   PatchListP Patch;		// Expressions depended on this symbol (for back-patching).
};

// From Lex.cpp:
extern Command CmdBuf[80];	// A tokenized line.
extern SymbolP SymTab[0x100];	// The symbol table (split by the upper hash byte).
void InitSymTab(void);		// Initialize the symbol table.
void TokenizeLine(char *Line);	// Tokenize a single line.

// From Exp.cpp:
extern PatchListP LastPatch;	// To patch the type for incomplete formulas.
int32_t GetExp(CommandP &Cmd);	// Calculate an expression.

// From Syn.cpp:
extern bool AtEnd;
void CompileLine(void);		// Compile a line into machine code.

// From Cas.cpp:
extern uint32_t CurPC;			// The current address.
extern uint8_t *RAM;			// The 64K RAM of the Z80.
extern int Loudness;
void Error(const char *Message);	// Print a fatal error message and exit.
void Log(int Loud, const char *Format, ...);
void List(const char *Format, ...);
void CheckPC(uint32_t PC);
