#define LineMax 0x100
#define NameMax 32
#define DEBUG 0

#include <cstdint>

enum Lexical {
   BadL,
   NumL, // a normal number
   OpL, // an opcode (0x00…0xff = ASCII code, >= 0x100 = opcodes [see below])
   SymL, // a symbol
   StrL // a string
};

enum Pseudo_t { _db = 0x100, _dm, _ds, _dw, _end, _equ, _org, _if, _endif, _else, _print };

// encoded opcode
typedef struct Command {
   Lexical Type;
   long Value;
} *CommandP;

// Expression for backpatching
typedef struct PatchList *PatchListP;
struct PatchList {
   PatchListP Next; // next entry in the list
   uint16_t Type; // How should the expression be patched in
// 0 = 1 byte
// 1 = 2 byte (low/high!)
// 2 = 1 byte, PC relative to patch address + 1
   uint32_t Addr; // patched address
   CommandP Cmd; // ptr to the formular
};

// entry for the symbol table
typedef struct Symbol *SymbolP;
struct Symbol {
   SymbolP Next; // next symbol
   uint16_t Hash; // hash value for the symbol name
   uint16_t Type; // Type: 0 = symbol; <>0 = opcode, etc.
   char Name[NameMax + 1]; // name of the symbol
   int32_t Value; // value of the symbol
   unsigned Defined:1; // true, if symbol is defined
   unsigned First:1; // true, if symbol is already valid
   PatchListP Patch; // expressions depended on this symbol (for backpatching)
};

// From Lex.cpp:
extern Command CmdBuf[80]; // a tokenized line
extern SymbolP SymTab[0x100]; // symbol table (split by the upper hash byte)
void InitSymTab(void); // initialize the symbol table
void TokenizeLine(char *Line); // tokenize a single line

// From Exp.cpp:
extern PatchListP LastPatch; // to patch the type for incomplete formulas
int32_t GetExp(CommandP &Cmd); // Calculate a formula

// From Syn.cpp:
void CompileLine(void); // Compile a single line into machine code

// From Cas.cpp:
extern uint32_t CurPC; // current address
extern uint8_t *RAM; // 64K RAM of the Z80
extern int Loudness;
void Error(const char *Message); // print a fatal error message and exit
void Log(int Loud, const char *Format, ...);
void List(const char *Format, ...);
void CheckPC(uint32_t PC);
