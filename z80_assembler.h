#define MAXLINELENGTH   256
#define MAXSYMBOLNAME   32
#define DEBUG           0

#include <cstdint>

typedef enum {
    ILLEGAL,
    NUM,            // a normal number
    OPCODE,         // an opcode (0…255 = ASCII code, >=256 = opcodes [see below])
    SYMBOL,         // a symbol
    STRING          // a string
} Type;

// encoded opcode
typedef struct {
    Type    typ;
    long    val;
} Command,*CommandP;

// Expression for backpatching
typedef struct RecalcList {
    struct RecalcList   *next;  // next entry in the list
    uint16_t            typ;    // How should the expression be patched in
                                // 0 = 1 byte
                                // 1 = 2 byte (low/high!)
                                // 2 = 1 byte, PC relative to patch address + 1
    uint16_t            adr;    // patched address
    CommandP            c;      // ptr to the formular
} RecalcList,*RecalcListP;

// entry for the symbol table
typedef struct Symbol {
    struct Symbol   *next;      // next symbol
    uint16_t        hash;       // hash value for the symbol name
    uint16_t        type;       // typ: 0 = symbol; <>0 = opcode, etc.
    char            name[MAXSYMBOLNAME+1];  // name of the symbol
    int32_t         val;        // value of the symbol
    unsigned        defined : 1;// true, if symbol is defined
    unsigned        first : 1;  // true, if symbol is already valid
    RecalcListP     recalc;     // expressions depended on this symbol (for backpatching)
} Symbol,*SymbolP;

extern Command      Cmd[80];            // a tokenized line
extern SymbolP      SymTab[256];        // symbol table (split by the upper hash value)
extern uint16_t     PC;                 // current address
extern uint8_t      *RAM;               // 64K RAM of the Z80

extern RecalcListP  LastRecalc;         // to patch the type for incomplete formulas

void        Error(const char* s);       // print a fatal error message

int32_t     CalcTerm(CommandP *c);      // calculate a formula

void        CompileLine(void);          // compile a line into machine code

void        InitSymTab(void);           // initialize the symbol table
void        TokenizeLine(char* sp);		// convert a line into tokens
