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

// Lexical typology.
#define LexC(R) ((R)&~0xff)	// Class
#define LexT(R) ((R)&~0xf)	// Sub-class
#define LexN(R) ((R)&7)		// Number

// Lexical classes for OpL type tokens.
#define _Lit 0x000	// 000⋯0ff: Character Literals.
#define _OpP 0x100	// 100⋯10b: Pseudo-Operators: db,dm,ds,dw,end,equ,org,if,endif,else,print,fill; but also 120-121: ">>","<<".
#define _Op 0x200	// 200⋯20f: Mnemonic classes; 280⋯281: Data and Addresses
#define _Reg 0x300	// 300⋯3ff: Registers; leads also to 500⋯5ff: (Register), 600⋯6ff: (Register+Index)
#define _Cc 0x400	// 400⋯407: Conditions: NZ,Z,NC,C,PO,PE,P,M

// Pseudo-Operators.
enum PseudoT { _db = 0x100, _dm, _ds, _dw, _end, _equ, _org, _if, _endif, _else, _print, _fill };

// Mnemonic operators and operator classes.
// _POp		in A,(P); out (P),A.
// _UnOp	one byte opcode, no parameter.
// _BinOp	two byte opcode, no parameter.
// _OpHL	two byte opcode, (HL) required; i.e. rrd (HL); rld (HL).
// _BitOp	bit n,Rb; res n,Rb; set n,Rb
// _im		im n (n	0,1,2).
// _AOp		AOp D,S (AOp	add,adc,sub,sbc,and,xor,or,cp).
// _IOp		IOp D (IOp	inc,dec), like _AOp with absolute address.
// _RefOp	jp [Cc,]Aw; call [Cc,]Aw; jr [Cc,]Js
// _ret		ret [Cc]
// _rst		rst n (n	00,08,10,18,20,28,30,38).
// _djnz	djnz Js.
// _ex		ex (SP),Rw; ex DE,HL; ex AF,AF'.
// _ld		ld D,S.
// _StOp	push Rw; pop Rw.
// _ShOp	ShOp D (ShOp: rr,rl,rrc,rlc,sra,sla,srl).
enum MnemonicT { _POp = 0x200, _UnOp, _BinOp, _OpHL, _BitOp, _im, _AOp, _IOp, _RefOp, _ret, _rst, _djnz, _ex, _ld, _StOp, _ShOp };

// Addresses
#define _W 0x280	// 280⋯281: Aw=(Dw),Dw
#define _Aw 0x280
#define _Dw 0x281

// Conditions.
enum ConditionT { _cNZ = _Cc, _cZ, _cNC, _cC, _cPO, _cPE, _cP, _cM };
#define IsCc0(Cc) ((Cc) >= _cNZ && (Cc) <= _cC)

// Registers and register classes.
#define _Rb 0x300
#define _Rw 0x310
#define _Rw1 0x320
#define _Rx 0x330
#define _Ri 0x340
#define _Xb 0x350
#define _Yb 0x360
enum RegisterT {
   _B = _Rb, _C, _D, _E, _H, _L, _pHL, _A,	// 300…307: Rb: B,C,D,E,H,L,(HL),A
   _BC = _Rw, _DE, _HL, _SP,			// 310…313: Rw: BC,DE,HL,SP
   _AF = _Rw1+3, _AFx,				//     323: Rw1:        ,AF,AF'
   _IX = _Rx, _IY,				// 330…331: Rx: IX,IY
   _R = _Ri, _I,				// 340…341: Ri: R,I
   _HX = _Xb+4, _LX, _pIX,			// 354…355: Xb: HX,LX,(IX)
   _HY = _Yb+4, _LY, _pIY,			// 364…365: Yb: HY,LY,(IY)
};
#define IsRbb(R) (LexT(R) >= _Rw && LexT(R) <= _Rx)
#define _p(R) ((R)+0x200)	// (R)
#define _x(R) ((R)+0x300)	// (R+Ds)

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
