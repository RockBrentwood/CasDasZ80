// DasZ80 Disassembler
// ───────────────────
// Originally Z80 Disassembler, it was created by Markuz Fritze (sarnau), as a small disassembler for a Z80 CPU in an afternoon.
// It is a command-line tool.
// Currently, the size of the ROM and entry points need to be hard-coded.
// It is gradually being migrated over to the design of the DAS disassembler, which currently only has a public-facing port for the 8051,
// (currently under https://github.com/RockBrentwood/CPU/tree/main/8051/csd4-archive/assem), and previously also had a local port for the 8080 and 8085.
// There is enough congruence in their designs to facilitate the migration to DAS.
//
// This program should compile with any C++ compiler.
// It only uses some ANSI C functions (look into ‟main()”).
// C++98 should be sufficient, compatibility should continue to hold for C++11 on up, for future changes.
// Current program versions can be compiled successfully with ‟gcc”.
// Z80DisAssembler has also compiled with ‟clang” without warnings, therefore DasZ80 should, as well.
//
// The program has two parts:
// ▪	Analyze the code.
// 	The disassembler tries to analyze what part of the binary data is program code and what part is data.
// 	It start with all hardware vectors of the Z80 (‟RST” opcodes, ‟NMI”) and parses all jumps via a recursive analyze via ‟ParseOpcode()”.
// 	Every opcode is marked in an array (‟OpcodesFlags”).
// 	There are some exceptions, the parser can't recognize:
// 	―	Self Modifying Code:
// 		A ROM shouldn't contain such code.
// 		To properly handle this would require run-time emulation.
// 		Examples where this occurs include self-unpacking, self-decrypting and/or self-loading code.
// 	―	Indirect Jumps: calculated branches with ‟JP (IY)”, ‟JP (IX)” or ‟JP (HL)”.
// 		The parser can't recognize them, either.
// 		The set of possible destinations has to be determined by analysis and added separately as entry points.
// 	―	Indirect Jumps: organized as jump tables.
// 		These are quite common in a ROM.
// 		The same applies here, as with calculated branches.
// 		If you find a jump table - like on Markuz' Futura aquarium computer - more entry points for ‟ParseOpcodes()” will need to be added.
// 	―	Unused code.
// 		This can be code that is reached from an unrecognized destination of an indirect jump, or as an unrecognized entry point.
// 		It if assumes a regular form, consisting particularly of bytes are in the range of printable characters, then it may be initialized data.
// 		It if assumes a regular form, like a block of 0's, then it may be uninitialized data.
// 		By default, it is treated as part of the data area of the program
// ▪	Disassembly of the code.
// 	With the help of the OpcodesFlags table the disassembler now creates the output.
// 	This subroutine is quite long.
// 	It disassembles one opcode at a specific address in ROM into a buffer.
// 	It is coded directly from a list of Z80 opcodes, so the handling of ‟IX” and ‟IY” could be optimized quite a lot.
//
// The subroutine ‟OpcodeLen()” returns the size of one opcode in bytes.
// It is called while parsing and while disassembling.
//
// The disassembler recognizes some hidden opcodes.
//
// If a routine wanted an ‟address” to the Z80 code, it is in fact an offset to the array of code.
// No pointers!
// Longs are not necessary for a Z80, because the standard Z80 only supports 64k.
//
// This program is freeware.
// It may not be used as a base for a commercial product!

#include "HexIn.h"
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

const uint32_t CODESIZE = 0x10000;

// memory for the code
static uint8_t Opcodes[CODESIZE];

// type of opcode
static uint8_t OpcodesFlags[CODESIZE];

// Flag per memory cell: opcode, operand or data
// bit 4 = 1, a JR or similar jumps to this address
enum { Empty, Opcode, Operand, Data };

static uint32_t RAM_low_addr = CODESIZE;
static uint32_t RAM_high_addr = 0;

static int verboseMode = 0;

static void MSG(int mode, const char *format, ...) {
   if (verboseMode >= mode) {
      va_list argptr;
      va_start(argptr, format);
      vfprintf(stderr, format, argptr);
      va_end(argptr);
   }
}

// debugging support function, print opcode as octal and binary
static void appendADE(char *s, size_t ssize, int a, char *prefix = nullptr) {
   if (verboseMode > 1) {
      uint8_t d = (a >> 3)&7;
      uint8_t e = a&7;
      size_t sl = strlen(s);
      snprintf(s + sl, ssize - sl, "%*c ", int(24 - strlen(s)), ';');
      sl = strlen(s);
      snprintf(s + sl, ssize - sl, "%02X: %d.%d.%d (%c%c.%c%c%c.%c%c%c)", a, a >> 6, d, e, a&0x80? '1': '0', a&0x40? '1': '0', d&0x04? '1': '0', d&0x02? '1': '0', d&0x01? '1': '0', e&0x04? '1': '0', e&0x02? '1': '0', e&0x01? '1': '0');
   }
}

// calculate the length of an opcode
static int OpcodeLen(uint16_t p) {
   int len = 1;
   switch (Opcodes[p]) { // Opcode
      case 0x06: // LD B,n
      case 0x0e: // LD C,n
      case 0x10: // DJNZ e
      case 0x16: // LD D,n
      case 0x18: // JR e
      case 0x1e: // LD E,n
      case 0x20: // JR NZ,e
      case 0x26: // LD H,n
      case 0x28: // JR Z,e
      case 0x2e: // LD L,n
      case 0x30: // JR NC,e
      case 0x36: // LD (HL),n
      case 0x38: // JR C,e
      case 0x3e: // LD A,n
      case 0xc6: // ADD A,n
      case 0xce: // ADC A,n
      case 0xd3: // OUT (n),A
      case 0xd6: // SUB n
      case 0xdb: // IN A,(n)
      case 0xde: // SBC A,n
      case 0xe6: // AND n
      case 0xee: // XOR n
      case 0xf6: // OR n
      case 0xfe: // CP n
      case 0xcb: // shift-,rotate-,bit-opcodes
         len = 2;
      break;
      case 0x01: // LD BC,nn'
      case 0x11: // LD DE,nn'
      case 0x21: // LD HL,nn'
      case 0x22: // LD (nn'),HL
      case 0x2a: // LD HL,(nn')
      case 0x31: // LD SP,(nn')
      case 0x32: // LD (nn'),A
      case 0x3a: // LD A,(nn')
      case 0xc2: // JP NZ,nn'
      case 0xc3: // JP nn'
      case 0xc4: // CALL NZ,nn'
      case 0xca: // JP Z,nn'
      case 0xcc: // CALL Z,nn'
      case 0xcd: // CALL nn'
      case 0xd2: // JP NC,nn'
      case 0xd4: // CALL NC,nn'
      case 0xda: // JP C,nn'
      case 0xdc: // CALL C,nn'
      case 0xe2: // JP PO,nn'
      case 0xe4: // CALL PO,nn'
      case 0xea: // JP PE,nn'
      case 0xec: // CALL PE,nn'
      case 0xf2: // JP P,nn'
      case 0xf4: // CALL P,nn'
      case 0xfa: // JP M,nn'
      case 0xfc: // CALL M,nn'
         len = 3;
      break;
      case 0xdd:
         len = 2;
         switch (Opcodes[p + 1]) { // 2nd part of the opcode
            case 0x34: // INC (IX+d)
            case 0x35: // DEC (IX+d)
            case 0x46: // LD B,(IX+d)
            case 0x4e: // LD C,(IX+d)
            case 0x56: // LD D,(IX+d)
            case 0x5e: // LD E,(IX+d)
            case 0x66: // LD H,(IX+d)
            case 0x6e: // LD L,(IX+d)
            case 0x70: // LD (IX+d),B
            case 0x71: // LD (IX+d),C
            case 0x72: // LD (IX+d),D
            case 0x73: // LD (IX+d),E
            case 0x74: // LD (IX+d),H
            case 0x75: // LD (IX+d),L
            case 0x77: // LD (IX+d),A
            case 0x7e: // LD A,(IX+d)
            case 0x86: // ADD A,(IX+d)
            case 0x8e: // ADC A,(IX+d)
            case 0x96: // SUB A,(IX+d)
            case 0x9e: // SBC A,(IX+d)
            case 0xa6: // AND (IX+d)
            case 0xae: // XOR (IX+d)
            case 0xb6: // OR (IX+d)
            case 0xbe: // CP (IX+d)
               len = 3;
            break;
            case 0x21: // LD IX,nn'
            case 0x22: // LD (nn'),IX
            case 0x2a: // LD IX,(nn')
            case 0x36: // LD (IX+d),n
            case 0xcb: // Rotation (IX+d)
               len = 4;
            break;
         }
      break;
      case 0xed:
         len = 2;
         switch (Opcodes[p + 1]) { // 2nd part of the opcode
            case 0x43: // LD (nn'),BC
            case 0x4b: // LD BC,(nn')
            case 0x53: // LD (nn'),DE
            case 0x5b: // LD DE,(nn')
            case 0x73: // LD (nn'),SP
            case 0x7b: // LD SP,(nn')
               len = 4;
            break;
         }
      break;
      case 0xfd:
         len = 2;
         switch (Opcodes[p + 1]) { // 2nd part of the opcode
            case 0x34: // INC (IY+d)
            case 0x35: // DEC (IY+d)
            case 0x46: // LD B,(IY+d)
            case 0x4e: // LD C,(IY+d)
            case 0x56: // LD D,(IY+d)
            case 0x5e: // LD E,(IY+d)
            case 0x66: // LD H,(IY+d)
            case 0x6e: // LD L,(IY+d)
            case 0x70: // LD (IY+d),B
            case 0x71: // LD (IY+d),C
            case 0x72: // LD (IY+d),D
            case 0x73: // LD (IY+d),E
            case 0x74: // LD (IY+d),H
            case 0x75: // LD (IY+d),L
            case 0x77: // LD (IY+d),A
            case 0x7e: // LD A,(IY+d)
            case 0x86: // ADD A,(IY+d)
            case 0x8e: // ADC A,(IY+d)
            case 0x96: // SUB A,(IY+d)
            case 0x9e: // SBC A,(IY+d)
            case 0xa6: // AND (IY+d)
            case 0xae: // XOR (IY+d)
            case 0xb6: // OR (IY+d)
            case 0xbe: // CP (IY+d)
               len = 3;
            break;
            case 0x21: // LD IY,nn'
            case 0x22: // LD (nn'),IY
            case 0x2a: // LD IY,(nn')
            case 0x36: // LD (IY+d),n
            case 0xcb: // Rotation,Bitop (IY+d)
               len = 4;
            break;
         }
      break;
   }
   return len;
}

// shorties for "next byte", "next next byte" and "next word"
#define BYTE_1 Opcodes[adr + 1]
#define BYTE_2 Opcodes[adr + 2]
#define WORD_1_2 (BYTE_1 + (BYTE_2 << 8))

static void ParseOpcodes(uint16_t adr) {
   int16_t i;
   uint32_t next;
   bool label = true;
   do {
      if (label) // set a label?
         OpcodesFlags[adr] |= 0x10; // mark the memory cell as a jump destination
      if ((OpcodesFlags[adr]&0x0f) == Opcode)
         break; // loop detected
      if ((OpcodesFlags[adr]&0x0f) == Operand) {
         printf("Illegal jump at addr %4.4XH\n", adr);
         return;
      }
      int len = OpcodeLen(adr);
      for (i = 0; i < len; i++)
         OpcodesFlags[adr + i] = Operand; // transfer the opcode
      OpcodesFlags[adr] = Opcode; // mark the beginning of the opcode
      if (label) { // define a label?
         OpcodesFlags[adr] |= 0x10; // yes
         label = false; // reset fkag
      }
      next = adr + len; // ptr to the next opcode
      switch (Opcodes[adr]) { // get that opcode
         case 0xca: // JP c,????
         case 0xc2:
         case 0xda:
         case 0xd2:
         case 0xea:
         case 0xe2:
         case 0xfa:
         case 0xf2:
            ParseOpcodes(WORD_1_2);
         break;
         case 0x28: // JR c,??
         case 0x20:
         case 0x38:
         case 0x30:
            ParseOpcodes(adr + 2 + BYTE_1);
         break;
         case 0xcc: // CALL c,????
         case 0xc4:
         case 0xdc:
         case 0xd4:
         case 0xec:
         case 0xe4:
         case 0xfc:
         case 0xf4:
            ParseOpcodes(WORD_1_2);
         break;
         case 0xc8: // RET c
         case 0xc0:
         case 0xd8:
         case 0xd0:
         case 0xe8:
         case 0xe0:
         case 0xf8:
         case 0xf0:
         break;
         case 0xc7: // RST 0
         case 0xcf: // RST 8
         case 0xd7: // RST 10
         case 0xdf: // RST 18
         case 0xe7: // RST 20
         case 0xef: // RST 28
         case 0xf7: // RST 30
         case 0xff: // RST 38
            ParseOpcodes(Opcodes[adr]&0x38);
         break;
         case 0x10: // DJNZ ??
            ParseOpcodes(adr + 2 + BYTE_1);
         break;
         case 0xc3: // JP ????
            next = WORD_1_2;
            label = true;
         break;
         case 0x18: // JR ??
            next = adr + 2 + BYTE_1;
            label = true;
         break;
         case 0xcd: // CALL ????
            ParseOpcodes(WORD_1_2);
         break;
         case 0xc9: // RET
         return;
         case 0xe9:
#if DETECT_JUMPTABLES
            puts("JP (HL) found"); // JP (HL)
            exit(-1);
#endif
         break;
         case 0xdd:
#if DETECT_JUMPTABLES
            if (BYTE_1 == 0xe9) { // JP (IX)
               puts("JP (IX) found");
               exit(-1);
            }
#endif
         break;
         case 0xfd:
#if DETECT_JUMPTABLES
            if (BYTE_1 == 0xe9) { // JP (IY)
               puts("JP (IY) found");
               exit(-1);
            }
#endif
         break;
         case 0xed:
            if (BYTE_1 == 0x4d) { // RETI
               return;
            } else if (BYTE_1 == 0x45) { // RETN
               return;
            }
         break;
      }
      adr = next;
   } while (1);
}

static void GenerateOutput(char *buf, size_t bufsize, const char *Format, ...) {
   va_list AP;
   va_start(AP, Format);
   vsnprintf(buf, bufsize, Format, AP);
   va_end(AP);
}
#define G(...) GenerateOutput(s, ssize, __VA_ARGS__)

// Disassemble
void Disassemble(uint16_t adr, char *s, size_t ssize) {
   uint8_t a = Opcodes[adr];
   uint8_t CB = 0, DDFD = 0, ED = 0; // prefix marker
   uint8_t d = (a >> 3)&7;
   uint8_t e = a&7;
   static const char *reg[8] = { "B", "C", "D", "E", "H", "L", "(HL)", "A" };
   static const char *dreg[4] = { "BC", "DE", "HL", "SP" };
   static const char *cond[8] = { "NZ", "Z", "NC", "C", "PO", "PE", "P", "M" };
   static const char *arith[8] = { "ADD     A,", "ADC     A,", "SUB", "SBC     A,", "AND", "XOR", "OR", "CP" };
   const char *ireg; // temp. index register string
   switch (a&0xc0) {
      case 0x00:
         switch (e) {
            case 0x00:
               switch (d) {
                  case 0x00:
                     G("NOP");
                  break;
                  case 0x01:
                     G("EX      AF,AF'");
                  break;
                  case 0x02:
                     G("DJNZ    $%4.4X", adr + 2 + BYTE_1);
                  break;
                  case 0x03:
                     G("JR      $%4.4X", adr + 2 + BYTE_1);
                  break;
                  default:
                     G("JR      %s,$%4.4X", cond[d&3], adr + 2 + BYTE_1);
                  break;
               }
            break;
            case 0x01:
               if (a&0x08) {
                  G("ADD     HL,%s", dreg[d >> 1]);
               } else {
                  G("LD      %s,$%4.4X", dreg[d >> 1], WORD_1_2);
               }
            break;
            case 0x02:
               switch (d) {
                  case 0x00:
                     G("LD      (BC),A");
                  break;
                  case 0x01:
                     G("LD      A,(BC)");
                  break;
                  case 0x02:
                     G("LD      (DE),A");
                  break;
                  case 0x03:
                     G("LD      A,(DE)");
                  break;
                  case 0x04:
                     G("LD      ($%4.4X),HL", WORD_1_2);
                  break;
                  case 0x05:
                     G("LD      HL,($%4.4X)", WORD_1_2);
                  break;
                  case 0x06:
                     G("LD      ($%4.4X),A", WORD_1_2);
                  break;
                  case 0x07:
                     G("LD      A,($%4.4X)", WORD_1_2);
                  break;
               }
            break;
            case 0x03:
               if (a&0x08)
                  G("DEC     %s", dreg[d >> 1]);
               else
                  G("INC     %s", dreg[d >> 1]);
            break;
            case 0x04:
               G("INC     %s", reg[d]);
            break;
            case 0x05:
               G("DEC     %s", reg[d]);
            break;
            case 0x06: // LD   d,n
               G("LD      %s,$%2.2X", reg[d], BYTE_1);
            break;
            case 0x07: {
               static const char *str[8] = { "RLCA", "RRCA", "RLA", "RRA", "DAA", "CPL", "SCF", "CCF" };
               G("%s", str[d]);
            }
            break;
         }
      break;
      case 0x40: // LD   d,s
         if (a == 0x76) {
            G("HALT");
         } else {
            G("LD      %s,%s", reg[d], reg[e]);
         }
      break;
      case 0x80:
         G("%-8s%s", arith[d], reg[e]);
      break;
      case 0xc0:
         switch (e) {
            case 0x00:
               G("RET     %s", cond[d]);
            break;
            case 0x01:
               if (d&1) {
                  switch (d >> 1) {
                     case 0x00:
                        G("RET");
                     break;
                     case 0x01:
                        G("EXX");
                     break;
                     case 0x02:
                        G("JP      (HL)");
                     break;
                     case 0x03:
                        G("LD      SP,HL");
                     break;
                  }
               } else {
                  if ((d >> 1) == 3)
                     G("POP     AF");
                  else
                     G("POP     %s", dreg[d >> 1]);
               }
            break;
            case 0x02:
               G("JP      %s,$%4.4X", cond[d], WORD_1_2);
            break;
            case 0x03:
               switch (d) {
                  case 0x00:
                     G("JP      $%4.4X", WORD_1_2);
                  break;
                  case 0x01: // 0xcb
                     CB = a;
                     a = Opcodes[++adr]; // get extended opcode
                     d = (a >> 3)&7;
                     e = a&7;
                     switch (a&0xc0) {
                        case 0x00: {
                           static const char *str[8] = { "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL" };
                           G("%-8s%s", str[d], reg[e]);
                        }
                        break;
                        case 0x40:
                           G("BIT     %d,%s", d, reg[e]);
                        break;
                        case 0x80:
                           G("RES     %d,%s", d, reg[e]);
                        break;
                        case 0xc0:
                           G("SET     %d,%s", d, reg[e]);
                        break;
                     }
                     if (verboseMode > 1)
                        appendADE(s, ssize, a); // debug for CB xx
                  break;
                  case 0x02:
                     G("OUT     ($%2.2X),A", BYTE_1);
                  break;
                  case 0x03:
                     G("IN      A,($%2.2X)", BYTE_1);
                  break;
                  case 0x04:
                     G("EX      (SP),HL");
                  break;
                  case 0x05:
                     G("EX      DE,HL");
                  break;
                  case 0x06:
                     G("DI");
                  break;
                  case 0x07:
                     G("EI");
                  break;
               }
            break;
            case 0x04:
               G("CALL    %s,$%4.4X", cond[d], WORD_1_2);
            break;
            case 0x05:
               if (d&1) {
                  switch (d >> 1) {
                     case 0x00:
                        G("CALL    $%4.4X", WORD_1_2);
                     break;
                     case 0x02: // 0xed
                        ED = a;
                        a = Opcodes[++adr]; // get extended opcode
                        d = (a >> 3)&7;
                        e = a&7;
                        switch (a&0xc0) {
                           case 0x40:
                              switch (e) {
                                 case 0x00:
                                    if (a == 0x70) // undoc: "in (c)" set flags but do not modify a reg
                                       G("IN      (C)");
                                    else
                                       G("IN      %s,(C)", reg[d]);
                                 break;
                                 case 0x01:
                                    if (a == 0x71) // undoc: "out (c), 0" output 0
                                       G("OUT     (C),0");
                                    else
                                       G("OUT     (C),%s", reg[d]);
                                 break;
                                 case 0x02:
                                    if (d&1)
                                       G("ADC     HL,%s", dreg[d >> 1]);
                                    else
                                       G("SBC     HL,%s", dreg[d >> 1]);
                                 break;
                                 case 0x03:
                                    if (d&1) {
                                       G("LD      %s,($%4.4X)", dreg[d >> 1], WORD_1_2);
                                    } else {
                                       G("LD      ($%4.4X),%s", WORD_1_2, dreg[d >> 1]);
                                    }
                                 break;
                                 case 0x04: {
                                    static const char *str[8] = { "NEG", "???", "???", "???", "???", "???", "???", "???" };
                                    G("%s", str[d]);
                                 }
                                 break;
                                 case 0x05: {
                                    static const char *str[8] = { "RETN", "RETI", "???", "???", "???", "???", "???", "???" };
                                    G("%s", str[d]);
                                 }
                                 break;
                                 case 0x06:
                                    G("IM      %d", d? d - 1: d); // ED 46, ED 56, ED 5E
                                 break;
                                 case 0x07: {
                                    static const char *str[8] = { "LD      I,A", "LD      R,A", "LD      A,I", "LD      A,R", "RRD", "RLD", "???", "???" };
                                    G("%s", str[d]);
                                 }
                                 break;
                              }
                           break;
                           case 0x80: {
                              static const char *str[32] = {
                                 "LDI", "CPI", "INI", "OUTI", "???", "???", "???", "???",
                                 "LDD ", "CPD", "IND", "OUTD", "???", "???", "???", "???",
                                 "LDIR", "CPIR", "INIR", "OTIR", "???", "???", "???", "???",
                                 "LDDR", "CPDR", "INDR", "OTDR", "???", "???", "???", "???"
                              };
                              G("%s", str[a&0x1f]);
                           }
                           break;
                        }
                        if (verboseMode > 1)
                           appendADE(s, ssize, a); // debug info for ED xx
                     break;
                     default: // 0x01 (0xdd) = IX, 0x03 (0xfd) = IY
                        DDFD = a;
                        ireg = (a&0x20)? "IY": "IX";
                        a = Opcodes[++adr]; // get extended opcode
                        d = (a >> 3)&7;
                        e = a&7;
                        switch (a) {
                           case 0x09:
                              G("ADD     %s,BC", ireg);
                           break;
                           case 0x19:
                              G("ADD     %s,DE", ireg);
                           break;
                           case 0x21:
                              G("LD      %s,$%4.4X", ireg, WORD_1_2);
                           break;
                           case 0x22:
                              G("LD      ($%4.4X),%s", WORD_1_2, ireg);
                           break;
                           case 0x23:
                              G("INC     %s", ireg);
                           break;
                           case 0x29:
                              G("ADD     %s,%s", ireg, ireg);
                           break;
                           case 0x2a:
                              G("LD      %s,($%4.4X)", ireg, WORD_1_2);
                           break;
                           case 0x2b:
                              G("DEC     %s", ireg);
                           break;
                           case 0x34:
                              G("INC     (%s+$%2.2X)", ireg, BYTE_1);
                           break;
                           case 0x35:
                              G("DEC     (%s+$%2.2X)", ireg, BYTE_1);
                           break;
                           case 0x36:
                              G("LD      (%s+$%2.2X),$%2.2X", ireg, BYTE_1, BYTE_2);
                           break;
                           case 0x39:
                              G("ADD     %s,SP", ireg);
                           break;
                           case 0x46:
                           case 0x4e:
                           case 0x56:
                           case 0x5e:
                           case 0x66:
                           case 0x6e:
                           case 0x7e:
                              G("LD      %s,(%s+$%2.2X)", reg[d], ireg, BYTE_1);
                           break;
                           case 0x70:
                           case 0x71:
                           case 0x72:
                           case 0x73:
                           case 0x74:
                           case 0x75:
                           case 0x77:
                              G("LD      (%s+$%2.2X),%s", ireg, BYTE_1, reg[e]);
                           break;
                           case 0x86:
                           case 0x8e:
                           case 0x96:
                           case 0x9e:
                           case 0xa6:
                           case 0xae:
                           case 0xb6:
                           case 0xbe:
                              G("%-8s(%s+$%2.2X)", arith[d], ireg, BYTE_1);
                           break;
                           case 0xe1:
                              G("POP     %s", ireg);
                           break;
                           case 0xe3:
                              G("EX      (SP),%s", ireg);
                           break;
                           case 0xe5:
                              G("PUSH    %s", ireg);
                           break;
                           case 0xe9:
                              G("JP      (%s)", ireg);
                           break;
                           case 0xf9:
                              G("LD      SP,%s", ireg);
                           break;
                           case 0xcb:
                              CB = a;
                              a = BYTE_2; // additional subopcodes
                              d = (a >> 3)&7;
                              switch (a&0xc0) {
                                 case 0x00: {
                                    static const char *str[8] = { "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL" };
                                    G("%-8s(%s+$%2.2X)", str[d], ireg, BYTE_1);
                                 }
                                 break;
                                 case 0x40:
                                    G("BIT     %d,(%s+$%2.2X)", d, ireg, BYTE_1);
                                 break;
                                 case 0x80:
                                    G("RES     %d,(%s+$%2.2X)", d, ireg, BYTE_1);
                                 break;
                                 case 0xc0:
                                    G("SET     %d,(%s+$%2.2X)", d, ireg, BYTE_1);
                                 break;
                              }
                              if (verboseMode > 1)
                                 appendADE(s, ssize, a); // debug info for DD/FD CB xx
                           break;
                        }
                        if (verboseMode > 1 && !CB)
                           appendADE(s, ssize, a); // debug info for DD/FD xx
                     break;
                  }
               } else
                  G("PUSH    %s", (d >> 1) == 3? "AF": dreg[d >> 1]);
            break;
            case 0x06:
               G("%-8s$%2.2X", arith[d], BYTE_1);
            break;
            case 0x07:
               G("RST     $%2.2X", a&0x38);
            break;
         }
      break;
   }
   if (verboseMode > 1 && !CB && !DDFD && !ED)
      appendADE(s, ssize, a); // this debug info only for non-prefixed opcodes
}

static void usage(const char *fullpath) {
   const char *progname = 0;
   char c;
   while ((c = *fullpath++))
      if (c == '/' || c == '\\')
         progname = fullpath;
   printf(
      "Usage:\n"
      "  %s [-fXX] [-oXXXX] [-p] [-r] [-v] [-x] <infile> [<outfile>]\n"
      "    -fXX    fill unused memory, XX = 0x00 .. 0xff\n"
      "    -oXXXX  org XXXX = 0x0000 .. 0xffff\n"
      "    -p      parse program flow\n"
      "    -r      parse also rst and nmi\n"
      "    -v      increase verbosity\n"
      "    -x      show hexdump\n",
      progname
   );
}

static bool load_bin(char *path, uint32_t offset);

// read, parse, disassemble and output
int main(int argc, char *argv[]) {
   char *inPath = 0, *outPath = 0;
   FILE *outfile;
#if 0
   const char *progname;
#endif
   bool hexdump = false;
   bool parse = false;
   bool rst_parse = false;
   uint32_t codesize;
   uint32_t adr = 0;
   char oneLine[256]; // output string
   fprintf(stderr, "DasZ80 - small disassembler for Z80 code\n");
   fprintf(stderr, "Based on TurboDis Z80 by Markus Fritze\n");
   int i, j;
   uint32_t offset = 0;
   uint32_t start = 0;
#if 0
   int len = 0;
#endif
   int fill = 0;
   int result;
   for (i = 1, j = 0; i < argc; i++) {
      if ('-' == argv[i][0]) {
         switch (argv[i][++j]) {
            case 'f': // fill
               if (argv[i][++j]) // "-fXX"
                  result = sscanf(argv[i] + j, "%x", &fill);
               else if (i < argc - 1) // "-f XX"
                  result = sscanf(argv[++i], "%x", &fill);
               if (result)
                  fill &= 0x00ff; // limit to byte size
               else {
                  fprintf(stderr, "Error: option -f needs a hexadecimal argument\n");
                  return 1;
               }
               j = 0; // end of this arg group
            break;
            case 'o': // offset
               if (argv[i][++j]) // "-oXXXX"
                  result = sscanf(argv[i] + j, "%x", &offset);
               else if (i < argc - 1) // "-o XXXX"
                  result = sscanf(argv[++i], "%x", &offset);
               if (result)
                  offset &= 0xffff; // limit to 64K
               else {
                  fprintf(stderr, "Error: option -o needs a hexadecimal argument\n");
                  return 1;
               }
               j = 0; // end of this arg group
            break;
            case 's': // start
               if (argv[i][++j]) // "-sXXXX"
                  result = sscanf(argv[i] + j, "%x", &start);
               else if (i < argc - 1) // "-s XXXX"
                  result = sscanf(argv[++i], "%x", &start);
               if (result)
                  start &= 0xffff; // limit to 64K
               else {
                  fprintf(stderr, "Error: option -s needs a hexadecimal argument\n");
                  return 1;
               }
               j = 0; // end of this arg group
            break;
            case 'p': // parse program flow
               parse = true;
            break;
            case 'r': // parse program flow
               rst_parse = true;
            break;
            case 'v':
               ++verboseMode;
            break;
            case 'x':
               hexdump = true;
            break;
            default:
               usage(argv[0]);
               return 1;
         }
         if (j && argv[i][j + 1]) { // one more arg char
            --i; // keep this arg group
            continue;
         }
         j = 0; // start from the beginning in next arg group
      } else {
         if (!inPath)
            inPath = argv[i];
         else if (!outPath)
            outPath = argv[i];
         else {
            usage(argv[0]);
            return 1;
         } // check next arg string
      }
   }
   memset(Opcodes, fill, sizeof(Opcodes));
   memset(OpcodesFlags, Empty, sizeof(OpcodesFlags));
   if (!load_bin(inPath, offset))
      return 1;
   codesize = RAM_high_addr + 1 - RAM_low_addr;
   offset = RAM_low_addr;
   codesize += offset;
   if (parse) {
      for (uint32_t i = RAM_low_addr; i <= RAM_high_addr; i++) // all data
         OpcodesFlags[i] = Data;
      if (rst_parse) {
         for (i = 0; i < 0x40; i += 0x08)
            if ((OpcodesFlags[i]&0x0f) == Data)
               ParseOpcodes(i); // parse RST vectors (if needed)
         if ((OpcodesFlags[0x66]&0x0f) == Data)
            ParseOpcodes(0x66); // parse also NMI vector (if needed)
      }
      ParseOpcodes(RAM_low_addr);
   }
   if (outPath) {
      outfile = fopen(outPath, "w");
      if (!outfile) {
         fprintf(stderr, "Error: cannot open outfile \"%s\"\n", outPath);
         return 1;
      }
   } else
      outfile = stdout;
   if (start >= offset)
      adr = start;
   else
      adr = offset;
   fprintf(outfile, "        ORG     $%04X\n", adr);
   while (adr <= RAM_high_addr) {
      uint32_t len, i;
      if ((OpcodesFlags[adr]&0x0f) == Data) {
         fprintf(outfile, "L%4.4X:  DEFB", (uint16_t)adr);
         for (i = 0; i < 16; i++) {
            if ((OpcodesFlags[adr + i]&0x0f) != Data || adr + i >= codesize)
               break;
            fprintf(outfile, "%s$%2.2X", (i)? ",": "    ", Opcodes[adr + i]);
         }
         fprintf(outfile, "\n");
         adr += i;
      } else {
         len = OpcodeLen(adr); // get length of opcode
         if (!hexdump) {
            if (OpcodesFlags[adr]&0x10)
               fprintf(outfile, "L%4.4X:  ", adr);
            else
               fprintf(outfile, "        ");
         } else {
            fprintf(outfile, "%4.4X    ", (uint16_t)adr);
            for (i = 0; i < len; i++)
               fprintf(outfile, "%2.2X ", Opcodes[adr + i]);
            for (i = 4; i > len; i--)
               fprintf(outfile, "   ");
            fprintf(outfile, "    ");
         }
         Disassemble(adr, oneLine, sizeof(oneLine));
         fprintf(outfile, "%s\n", oneLine);
         adr += len;
      }
   }
   fclose(outfile);
}

// the z80 format is used by the z80-asm
// http://wwwhomes.uni-bielefeld.de/achim/z80-asm.html
// *.z80 files are bin files with a header telling the bin offset
//	struct z80_header {
//		const char *MAGIC = Z80MAGIC;
//		uint16_t offset;
//	}
// reads header of a file and tests if it's Z80 ASM file, reads address
// return value: 0=OK, 1=this is not a z80 asm file, 2,3=seek malfunction
int read_header(FILE *stream, uint32_t *address, uint32_t *len) {
   const char *Z80MAGIC = "Z80ASM\032\n";
   char tmp[9];
   unsigned char c[2];
   unsigned a, b;
   int ret = 0;
   b = strlen(Z80MAGIC);
   tmp[b] = 0;
   a = 0;
   if ((a = fread(tmp, 1, b, stream)) != b)
      ret = 1;
   else if (strcmp(tmp, Z80MAGIC))
      ret = 1;
   else if (fread(c, 1, 2, stream) != 2)
      ret = 1;
   else {
      *address = (c[1] << 8) | c[0];
      a = b + 2;
   }
   if (fseek(stream, 0, SEEK_END))
      ret = 2;
   else if ((b = ftell(stream)) < a)
      ret = 2;
   else
      *len = b - a;
   if (fseek(stream, a, SEEK_SET))
      ret = 3;
   return ret;
}

static bool load_bin(char *path, uint32_t offset) {
#if 0
   int address;
#endif
   uint32_t size;
   char buffer[256];
   FILE *fp = fopen(path, "rb");
   if (!fp)
      return false;
   if (strlen(path) > 4 && !strcmp(path + strlen(path) - 4, ".hex")) {
      struct ihex_state ihex;
      ihex_begin_read(&ihex);
      while (fgets(buffer, sizeof(buffer), fp))
         ihex_read_bytes(&ihex, buffer, strlen(buffer));
      ihex_end_read(&ihex);
      fclose(fp);
      return true;
   } else if (strlen(path) > 4 && !strcmp(path + strlen(path) - 4, ".z80")) {
      int ret = read_header(fp, &offset, &size);
      if (ret) {
         fprintf(stderr, "Error %d reading z80 file \"%s\"\n", ret, path);
         return false;
      }
   } else { // bin file
      fseek(fp, 0, SEEK_END);
      size = ftell(fp);
      fseek(fp, 0, SEEK_SET);
   }
   if (size < 1 || size > CODESIZE - offset) {
      fprintf(stderr, "File size (%u bytes) exceeds available RAM size (%u bytes)\n", size, CODESIZE - offset);
      fclose(fp);
      return false;
   }
   if (size != (unsigned)fread(Opcodes + offset, 1, (size_t)size, fp)) {
      fprintf(stderr, "Cannot read file: \"%s\"\n", path);
      fclose(fp);
      return false;
   }
   RAM_low_addr = offset;
   RAM_high_addr = offset + size - 1;
   MSG(2, "Loaded %d data bytes from \"%s\" into RAM region [0x%04X...0x%04X]\n", size, path, RAM_low_addr, RAM_high_addr);
   fclose(fp);
   return true;
}

// callback from HexIn.c when data has arrived
ihex_bool_t ihex_data_read(struct ihex_state *ihex, ihex_record_type_t type, ihex_bool_t error) {
   static uint32_t hex_data_size = 0;
   error = error || (ihex->length < ihex->line_length);
   if (type == IHEX_DATA_RECORD && !error) {
      MSG(4, "IHEX addr: $%04X, data len: %d\n", IHEX_LINEAR_ADDRESS(ihex), ihex->length);
      memcpy(Opcodes + IHEX_LINEAR_ADDRESS(ihex), ihex->data, ihex->length);
      if (IHEX_LINEAR_ADDRESS(ihex) < RAM_low_addr)
         RAM_low_addr = IHEX_LINEAR_ADDRESS(ihex);
      if (IHEX_LINEAR_ADDRESS(ihex) + ihex->length >= RAM_high_addr)
         RAM_high_addr = IHEX_LINEAR_ADDRESS(ihex) + ihex->length - 1;
      hex_data_size += ihex->length;
   } else if (type == IHEX_END_OF_FILE_RECORD) {
      MSG(4, "IHEX EOF\n");
      MSG(1, "Loaded %d data bytes from hexfile into RAM region [0x%04X...0x%04X]\n", hex_data_size, RAM_low_addr, RAM_high_addr);
      if (hex_data_size != RAM_high_addr + 1 - RAM_low_addr)
         MSG(1, "(size: %d Bytes)\n", RAM_high_addr + 1 - RAM_low_addr);
      else
         MSG(1, "");
   }
   return !error;
}
