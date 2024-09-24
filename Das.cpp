// DasZ80 Disassembler
// ───────────────────
// The program has two parts:
// ▪	Analyze the code.
// 	The disassembler tries to analyze what part of the binary data is program code and what part is data.
// 	It start with all hardware vectors of the Z80 (‟RST” opcodes, ‟NMI”) and parses all jumps via a recursive analyze via ‟ParseOpcode()”.
// 	Every opcode is marked in an array (‟Mode”).
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
// 	With the help of the Mode table the disassembler now creates the output.
// 	This subroutine is quite long.
// 	It disassembles one opcode at a specific address in ROM into a buffer.
// 	It is coded directly from a list of Z80 opcodes, so the handling of ‟IX” and ‟IY” could be optimized quite a lot.
//
// The subroutine ‟OpLen()” returns the size of one opcode in bytes.
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

static const uint32_t CodeMax = 0x10000;

// memory for the code
static uint8_t Code[CodeMax];

// type of opcode
static uint8_t Mode[CodeMax];

// Flag per memory cell: opcode, operand or data
// bit 4 = 1, a JR or similar jumps to this address
enum { Empty, Opcode, Operand, Data };

static uint32_t LoRAM = CodeMax;
static uint32_t HiRAM = 0;

static int Loudness = 0;

static void Log(int Loud, const char *Format, ...) {
   if (Loudness >= Loud) {
      va_list AP;
      va_start(AP, Format);
      vfprintf(stderr, Format, AP);
      va_end(AP);
   }
}

// debugging support function, print opcode as octal and binary
static void ShowOp(char *Buf, size_t Max, int Op, char *Prefix = nullptr) {
   if (Loudness > 1) {
      uint8_t Y = (Op >> 3)&7;
      uint8_t Z = Op&7;
      size_t N = strlen(Buf);
      snprintf(Buf + N, Max - N, "%*c ", int(24 - strlen(Buf)), ';');
      N = strlen(Buf);
      snprintf(Buf + N, Max - N, "%02X: %d.%d.%d (%c%c.%c%c%c.%c%c%c)", Op, Op >> 6, Y, Z, Op&0200? '1': '0', Op&0100? '1': '0', Y&4? '1': '0', Y&2? '1': '0', Y&1? '1': '0', Z&4? '1': '0', Z&2? '1': '0', Z&1? '1': '0');
   }
}

// calculate the length of an opcode
static int OpLen(uint16_t IP) {
   int Len = 1;
   switch (Code[IP]) { // Opcode
      case 0006: // LD B,n
      case 0016: // LD C,n
      case 0020: // DJNZ e
      case 0026: // LD D,n
      case 0030: // JR e
      case 0036: // LD E,n
      case 0040: // JR NZ,e
      case 0046: // LD H,n
      case 0050: // JR Z,e
      case 0056: // LD L,n
      case 0060: // JR NC,e
      case 0066: // LD (HL),n
      case 0070: // JR C,e
      case 0076: // LD A,n
      case 0306: // ADD A,n
      case 0316: // ADC A,n
      case 0323: // OUT (n),A
      case 0326: // SUB n
      case 0333: // IN A,(n)
      case 0336: // SBC A,n
      case 0346: // AND n
      case 0356: // XOR n
      case 0366: // OR n
      case 0376: // CP n
      case 0313: // shift-,rotate-,bit-opcodes
         Len = 2;
      break;
      case 0001: // LD BC,nn'
      case 0021: // LD DE,nn'
      case 0041: // LD HL,nn'
      case 0042: // LD (nn'),HL
      case 0052: // LD HL,(nn')
      case 0061: // LD SP,(nn')
      case 0062: // LD (nn'),A
      case 0072: // LD A,(nn')
      case 0302: // JP NZ,nn'
      case 0303: // JP nn'
      case 0304: // CALL NZ,nn'
      case 0312: // JP Z,nn'
      case 0314: // CALL Z,nn'
      case 0315: // CALL nn'
      case 0322: // JP NC,nn'
      case 0324: // CALL NC,nn'
      case 0332: // JP C,nn'
      case 0334: // CALL C,nn'
      case 0342: // JP PO,nn'
      case 0344: // CALL PO,nn'
      case 0352: // JP PE,nn'
      case 0354: // CALL PE,nn'
      case 0362: // JP P,nn'
      case 0364: // CALL P,nn'
      case 0372: // JP M,nn'
      case 0374: // CALL M,nn'
         Len = 3;
      break;
      case 0335:
         Len = 2;
         switch (Code[IP + 1]) { // 2nd part of the opcode
            case 0064: // INC (IX+d)
            case 0065: // DEC (IX+d)
            case 0106: // LD B,(IX+d)
            case 0116: // LD C,(IX+d)
            case 0126: // LD D,(IX+d)
            case 0136: // LD E,(IX+d)
            case 0146: // LD H,(IX+d)
            case 0156: // LD L,(IX+d)
            case 0160: // LD (IX+d),B
            case 0161: // LD (IX+d),C
            case 0162: // LD (IX+d),D
            case 0163: // LD (IX+d),E
            case 0164: // LD (IX+d),H
            case 0165: // LD (IX+d),L
            case 0167: // LD (IX+d),A
            case 0176: // LD A,(IX+d)
            case 0206: // ADD A,(IX+d)
            case 0216: // ADC A,(IX+d)
            case 0226: // SUB A,(IX+d)
            case 0236: // SBC A,(IX+d)
            case 0246: // AND (IX+d)
            case 0256: // XOR (IX+d)
            case 0266: // OR (IX+d)
            case 0276: // CP (IX+d)
               Len = 3;
            break;
            case 0041: // LD IX,nn'
            case 0042: // LD (nn'),IX
            case 0052: // LD IX,(nn')
            case 0066: // LD (IX+d),n
            case 0313: // Rotation (IX+d)
               Len = 4;
            break;
         }
      break;
      case 0355:
         Len = 2;
         switch (Code[IP + 1]) { // 2nd part of the opcode
            case 0103: // LD (nn'),BC
            case 0113: // LD BC,(nn')
            case 0123: // LD (nn'),DE
            case 0133: // LD DE,(nn')
            case 0163: // LD (nn'),SP
            case 0173: // LD SP,(nn')
               Len = 4;
            break;
         }
      break;
      case 0375:
         Len = 2;
         switch (Code[IP + 1]) { // 2nd part of the opcode
            case 0064: // INC (IY+d)
            case 0065: // DEC (IY+d)
            case 0106: // LD B,(IY+d)
            case 0116: // LD C,(IY+d)
            case 0126: // LD D,(IY+d)
            case 0136: // LD E,(IY+d)
            case 0146: // LD H,(IY+d)
            case 0156: // LD L,(IY+d)
            case 0160: // LD (IY+d),B
            case 0161: // LD (IY+d),C
            case 0162: // LD (IY+d),D
            case 0163: // LD (IY+d),E
            case 0164: // LD (IY+d),H
            case 0165: // LD (IY+d),L
            case 0167: // LD (IY+d),A
            case 0176: // LD A,(IY+d)
            case 0206: // ADD A,(IY+d)
            case 0216: // ADC A,(IY+d)
            case 0226: // SUB A,(IY+d)
            case 0236: // SBC A,(IY+d)
            case 0246: // AND (IY+d)
            case 0256: // XOR (IY+d)
            case 0266: // OR (IY+d)
            case 0276: // CP (IY+d)
               Len = 3;
            break;
            case 0041: // LD IY,nn'
            case 0042: // LD (nn'),IY
            case 0052: // LD IY,(nn')
            case 0066: // LD (IY+d),n
            case 0313: // Rotation,Bitop (IY+d)
               Len = 4;
            break;
         }
      break;
   }
   return Len;
}

// shorties for "next byte", "next next byte" and "next word"
#define Byte1 Code[IP + 1]
#define Byte2 Code[IP + 2]
#define Word12 (Byte1 + (Byte2 << 8))

static void ParseOpcodes(uint16_t IP) {
   bool Label = true;
   do {
      if (Label) // set a label?
         Mode[IP] |= 0x10; // mark the memory cell as a jump destination
      if ((Mode[IP]&0x0f) == Opcode)
         break; // loop detected
      if ((Mode[IP]&0x0f) == Operand) {
         printf("Illegal jump at addr %4.4XH\n", IP);
         return;
      }
      int N = OpLen(IP);
      for (int16_t n = 0; n < N; n++)
         Mode[IP + n] = Operand; // transfer the opcode
      Mode[IP] = Opcode; // mark the beginning of the opcode
      if (Label) { // define a label?
         Mode[IP] |= 0x10; // yes
         Label = false; // reset fkag
      }
      uint32_t NextIP = IP + N; // ptr to the next opcode
      switch (Code[IP]) { // get that opcode
         case 0312: // JP c,????
         case 0302:
         case 0332:
         case 0322:
         case 0352:
         case 0342:
         case 0372:
         case 0362:
            ParseOpcodes(Word12);
         break;
         case 0050: // JR c,??
         case 0040:
         case 0070:
         case 0060:
            ParseOpcodes(IP + 2 + Byte1);
         break;
         case 0314: // CALL c,????
         case 0304:
         case 0334:
         case 0324:
         case 0354:
         case 0344:
         case 0374:
         case 0364:
            ParseOpcodes(Word12);
         break;
         case 0310: // RET c
         case 0300:
         case 0330:
         case 0320:
         case 0350:
         case 0340:
         case 0370:
         case 0360:
         break;
         case 0307: // RST 00q
         case 0317: // RST 10q
         case 0327: // RST 20q
         case 0337: // RST 30q
         case 0347: // RST 40q
         case 0357: // RST 50q
         case 0367: // RST 60q
         case 0377: // RST 70q
            ParseOpcodes(Code[IP]&070);
         break;
         case 0020: // DJNZ ??
            ParseOpcodes(IP + 2 + Byte1);
         break;
         case 0303: // JP ????
            NextIP = Word12;
            Label = true;
         break;
         case 0030: // JR ??
            NextIP = IP + 2 + Byte1;
            Label = true;
         break;
         case 0315: // CALL ????
            ParseOpcodes(Word12);
         break;
         case 0311: // RET
         return;
         case 0351:
#if DETECT_JUMPTABLES
            puts("JP (HL) found"); // JP (HL)
            exit(-1);
#endif
         break;
         case 0335:
#if DETECT_JUMPTABLES
            if (Byte1 == 0351) { // JP (IX)
               puts("JP (IX) found");
               exit(-1);
            }
#endif
         break;
         case 0375:
#if DETECT_JUMPTABLES
            if (Byte1 == 0351) { // JP (IY)
               puts("JP (IY) found");
               exit(-1);
            }
#endif
         break;
         case 0355:
            if (Byte1 == 0115) { // RETI
               return;
            } else if (Byte1 == 0105) { // RETN
               return;
            }
         break;
      }
      IP = NextIP;
   } while (1);
}

static void _Gen(char *Buf, size_t BufN, const char *Format, ...) {
   va_list AP;
   va_start(AP, Format);
   vsnprintf(Buf, BufN, Format, AP);
   va_end(AP);
}
#define Gen(...) _Gen(Buf, BufN, __VA_ARGS__)

// Disassemble
static void Disassemble(uint16_t IP, char *Buf, size_t BufN) {
   uint8_t Op = Code[IP];
   uint8_t _313 = 0, _335_375 = 0, _355 = 0; // prefix marker
   uint8_t Y = (Op >> 3)&7;
   uint8_t Z = Op&7;
   static const char *Rb[8] = { "B", "C", "D", "E", "H", "L", "(HL)", "A" };
   static const char *Rw[4] = { "BC", "DE", "HL", "SP" };
   static const char *Cc[8] = { "NZ", "Z", "NC", "C", "PO", "PE", "P", "M" };
   static const char *AOp[8] = { "ADD     A,", "ADC     A,", "SUB", "SBC     A,", "AND", "XOR", "OR", "CP" };
   const char *Rx; // temp. index register string
   switch (Op&0300) {
      case 0000:
         switch (Z) {
            case 0:
               switch (Y) {
                  case 0:
                     Gen("NOP");
                  break;
                  case 1:
                     Gen("EX      AF,AF'");
                  break;
                  case 2:
                     Gen("DJNZ    $%4.4X", IP + 2 + Byte1);
                  break;
                  case 3:
                     Gen("JR      $%4.4X", IP + 2 + Byte1);
                  break;
                  default:
                     Gen("JR      %s,$%4.4X", Cc[Y&3], IP + 2 + Byte1);
                  break;
               }
            break;
            case 1:
               if (Op&010) {
                  Gen("ADD     HL,%s", Rw[Y >> 1]);
               } else {
                  Gen("LD      %s,$%4.4X", Rw[Y >> 1], Word12);
               }
            break;
            case 2:
               switch (Y) {
                  case 0:
                     Gen("LD      (BC),A");
                  break;
                  case 1:
                     Gen("LD      A,(BC)");
                  break;
                  case 2:
                     Gen("LD      (DE),A");
                  break;
                  case 3:
                     Gen("LD      A,(DE)");
                  break;
                  case 4:
                     Gen("LD      ($%4.4X),HL", Word12);
                  break;
                  case 5:
                     Gen("LD      HL,($%4.4X)", Word12);
                  break;
                  case 6:
                     Gen("LD      ($%4.4X),A", Word12);
                  break;
                  case 7:
                     Gen("LD      A,($%4.4X)", Word12);
                  break;
               }
            break;
            case 3:
               if (Op&010)
                  Gen("DEC     %s", Rw[Y >> 1]);
               else
                  Gen("INC     %s", Rw[Y >> 1]);
            break;
            case 4:
               Gen("INC     %s", Rb[Y]);
            break;
            case 5:
               Gen("DEC     %s", Rb[Y]);
            break;
            case 6: // LD   Y,n
               Gen("LD      %s,$%2.2X", Rb[Y], Byte1);
            break;
            case 7: {
               static const char *S0Op[8] = { "RLCA", "RRCA", "RLA", "RRA", "DAA", "CPL", "SCF", "CCF" };
               Gen("%s", S0Op[Y]);
            }
            break;
         }
      break;
      case 0100: // LD   d,s
         if (Op == 0166) {
            Gen("HALT");
         } else {
            Gen("LD      %s,%s", Rb[Y], Rb[Z]);
         }
      break;
      case 0200:
         Gen("%-8s%s", AOp[Y], Rb[Z]);
      break;
      case 0300:
         switch (Z) {
            case 0:
               Gen("RET     %s", Cc[Y]);
            break;
            case 1:
               if (Y&1) {
                  switch (Y >> 1) {
                     case 0:
                        Gen("RET");
                     break;
                     case 1:
                        Gen("EXX");
                     break;
                     case 2:
                        Gen("JP      (HL)");
                     break;
                     case 3:
                        Gen("LD      SP,HL");
                     break;
                  }
               } else {
                  if ((Y >> 1) == 3)
                     Gen("POP     AF");
                  else
                     Gen("POP     %s", Rw[Y >> 1]);
               }
            break;
            case 2:
               Gen("JP      %s,$%4.4X", Cc[Y], Word12);
            break;
            case 3:
               switch (Y) {
                  case 0:
                     Gen("JP      $%4.4X", Word12);
                  break;
                  case 1: // 0313
                     _313 = Op;
                     Op = Code[++IP]; // get extended opcode
                     Y = (Op >> 3)&7;
                     Z = Op&7;
                     switch (Op&0300) {
                        case 0000: {
                           static const char *ShOp[8] = { "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL" };
                           Gen("%-8s%s", ShOp[Y], Rb[Z]);
                        }
                        break;
                        case 0100:
                           Gen("BIT     %d,%s", Y, Rb[Z]);
                        break;
                        case 0200:
                           Gen("RES     %d,%s", Y, Rb[Z]);
                        break;
                        case 0300:
                           Gen("SET     %d,%s", Y, Rb[Z]);
                        break;
                     }
                     if (Loudness > 1)
                        ShowOp(Buf, BufN, Op); // debug for 313 xx
                  break;
                  case 2:
                     Gen("OUT     ($%2.2X),A", Byte1);
                  break;
                  case 3:
                     Gen("IN      A,($%2.2X)", Byte1);
                  break;
                  case 4:
                     Gen("EX      (SP),HL");
                  break;
                  case 5:
                     Gen("EX      DE,HL");
                  break;
                  case 6:
                     Gen("DI");
                  break;
                  case 7:
                     Gen("EI");
                  break;
               }
            break;
            case 4:
               Gen("CALL    %s,$%4.4X", Cc[Y], Word12);
            break;
            case 5:
               if (Y&1) {
                  switch (Y >> 1) {
                     case 0:
                        Gen("CALL    $%4.4X", Word12);
                     break;
                     case 2: // 0355
                        _355 = Op;
                        Op = Code[++IP]; // get extended opcode
                        Y = (Op >> 3)&7;
                        Z = Op&7;
                        switch (Op&0300) {
                           case 0100:
                              switch (Z) {
                                 case 0:
                                    if (Op == 0160) // undoc: "in (c)" set flags but do not modify a reg
                                       Gen("IN      (C)");
                                    else
                                       Gen("IN      %s,(C)", Rb[Y]);
                                 break;
                                 case 1:
                                    if (Op == 0161) // undoc: "out (c), 0" output 0
                                       Gen("OUT     (C),0");
                                    else
                                       Gen("OUT     (C),%s", Rb[Y]);
                                 break;
                                 case 2:
                                    if (Y&1)
                                       Gen("ADC     HL,%s", Rw[Y >> 1]);
                                    else
                                       Gen("SBC     HL,%s", Rw[Y >> 1]);
                                 break;
                                 case 3:
                                    if (Y&1) {
                                       Gen("LD      %s,($%4.4X)", Rw[Y >> 1], Word12);
                                    } else {
                                       Gen("LD      ($%4.4X),%s", Word12, Rw[Y >> 1]);
                                    }
                                 break;
                                 case 4: {
                                    static const char *_355_1Y4[8] = { "NEG", "???", "???", "???", "???", "???", "???", "???" };
                                    Gen("%s", _355_1Y4[Y]);
                                 }
                                 break;
                                 case 5: {
                                    static const char *_355_1Y5[8] = { "RETN", "RETI", "???", "???", "???", "???", "???", "???" };
                                    Gen("%s", _355_1Y5[Y]);
                                 }
                                 break;
                                 case 6:
                                    Gen("IM      %d", Y? Y - 1: Y); // 355 106, 355 126, 355 136
                                 break;
                                 case 7: {
                                    static const char *_355_1Y7[8] = { "LD      I,A", "LD      R,A", "LD      A,I", "LD      A,R", "RRD", "RLD", "???", "???" };
                                    Gen("%s", _355_1Y7[Y]);
                                 }
                                 break;
                              }
                           break;
                           case 0200: {
                              static const char *_355_2YZ[040] = {
                                 "LDI", "CPI", "INI", "OUTI", "???", "???", "???", "???",
                                 "LDD ", "CPD", "IND", "OUTD", "???", "???", "???", "???",
                                 "LDIR", "CPIR", "INIR", "OTIR", "???", "???", "???", "???",
                                 "LDDR", "CPDR", "INDR", "OTDR", "???", "???", "???", "???"
                              };
                              Gen("%s", _355_2YZ[Op&037]);
                           }
                           break;
                        }
                        if (Loudness > 1)
                           ShowOp(Buf, BufN, Op); // debug info for 355 xx
                     break;
                     default: // 1 (0335) = IX, 3 (0375) = IY
                        _335_375 = Op;
                        Rx = (Op&040)? "IY": "IX";
                        Op = Code[++IP]; // get extended opcode
                        Y = (Op >> 3)&7;
                        Z = Op&7;
                        switch (Op) {
                           case 0011:
                              Gen("ADD     %s,BC", Rx);
                           break;
                           case 0031:
                              Gen("ADD     %s,DE", Rx);
                           break;
                           case 0041:
                              Gen("LD      %s,$%4.4X", Rx, Word12);
                           break;
                           case 0042:
                              Gen("LD      ($%4.4X),%s", Word12, Rx);
                           break;
                           case 0043:
                              Gen("INC     %s", Rx);
                           break;
                           case 0051:
                              Gen("ADD     %s,%s", Rx, Rx);
                           break;
                           case 0052:
                              Gen("LD      %s,($%4.4X)", Rx, Word12);
                           break;
                           case 0053:
                              Gen("DEC     %s", Rx);
                           break;
                           case 0064:
                              Gen("INC     (%s+$%2.2X)", Rx, Byte1);
                           break;
                           case 0065:
                              Gen("DEC     (%s+$%2.2X)", Rx, Byte1);
                           break;
                           case 0066:
                              Gen("LD      (%s+$%2.2X),$%2.2X", Rx, Byte1, Byte2);
                           break;
                           case 0071:
                              Gen("ADD     %s,SP", Rx);
                           break;
                           case 0106:
                           case 0116:
                           case 0126:
                           case 0136:
                           case 0146:
                           case 0156:
                           case 0176:
                              Gen("LD      %s,(%s+$%2.2X)", Rb[Y], Rx, Byte1);
                           break;
                           case 0160:
                           case 0161:
                           case 0162:
                           case 0163:
                           case 0164:
                           case 0165:
                           case 0167:
                              Gen("LD      (%s+$%2.2X),%s", Rx, Byte1, Rb[Z]);
                           break;
                           case 0206:
                           case 0216:
                           case 0226:
                           case 0236:
                           case 0246:
                           case 0256:
                           case 0266:
                           case 0276:
                              Gen("%-8s(%s+$%2.2X)", AOp[Y], Rx, Byte1);
                           break;
                           case 0341:
                              Gen("POP     %s", Rx);
                           break;
                           case 0343:
                              Gen("EX      (SP),%s", Rx);
                           break;
                           case 0345:
                              Gen("PUSH    %s", Rx);
                           break;
                           case 0351:
                              Gen("JP      (%s)", Rx);
                           break;
                           case 0371:
                              Gen("LD      SP,%s", Rx);
                           break;
                           case 0313:
                              _313 = Op;
                              Op = Byte2; // additional subopcodes
                              Y = (Op >> 3)&7;
                              switch (Op&0300) {
                                 case 0000: {
                                    static const char *ShOp[8] = { "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL" };
                                    Gen("%-8s(%s+$%2.2X)", ShOp[Y], Rx, Byte1);
                                 }
                                 break;
                                 case 0100:
                                    Gen("BIT     %d,(%s+$%2.2X)", Y, Rx, Byte1);
                                 break;
                                 case 0200:
                                    Gen("RES     %d,(%s+$%2.2X)", Y, Rx, Byte1);
                                 break;
                                 case 0300:
                                    Gen("SET     %d,(%s+$%2.2X)", Y, Rx, Byte1);
                                 break;
                              }
                              if (Loudness > 1)
                                 ShowOp(Buf, BufN, Op); // debug info for 335/375 313 xx
                           break;
                        }
                        if (Loudness > 1 && !_313)
                           ShowOp(Buf, BufN, Op); // debug info for 335/375 xx
                     break;
                  }
               } else
                  Gen("PUSH    %s", (Y >> 1) == 3? "AF": Rw[Y >> 1]);
            break;
            case 6:
               Gen("%-8s$%2.2X", AOp[Y], Byte1);
            break;
            case 7:
               Gen("RST     $%2.2X", Op&070);
            break;
         }
      break;
   }
   if (Loudness > 1 && !_313 && !_335_375 && !_355)
      ShowOp(Buf, BufN, Op); // this debug info only for non-prefixed opcodes
}

static void Usage(const char *Path) {
   const char *App = 0;
   for (char Ch; (Ch = *Path++); )
      if (Ch == '/' || Ch == '\\')
         App = Path;
   printf(
      "Usage:\n"
      "  %s [-fXX] [-oXXXX] [-p] [-r] [-v] [-x] <infile> [<outfile>]\n"
      "    -fXX    fill unused memory, XX = 0x00 .. 0xff\n"
      "    -oXXXX  org XXXX = 0x0000 .. 0xffff\n"
      "    -p      parse program flow\n"
      "    -r      parse also rst and nmi\n"
      "    -v      increase verbosity\n"
      "    -x      show hexdump\n",
      App
   );
}

// the z80 format is used by the z80-asm
// http://wwwhomes.uni-bielefeld.de/achim/z80-asm.html
// *.z80 files are bin files with a header telling the bin offset
//	struct z80_header {
//		const char *Signature = "Z80ASM" "\032" "\n";
//		uint16_t Offset;
//	}
// reads header of a file and tests if it's Z80 ASM file, reads address
// return value: 0=OK, 1=this is not a z80 asm file, 2,3=seek malfunction
static int GetHeader(FILE *InF, uint32_t &Address, uint32_t &Size) {
   const char *Signature = "Z80ASM" "\032" "\n";
   unsigned char Buf[2];
   unsigned SigN = strlen(Signature);
   char InBuf[9];
   InBuf[SigN] = '\0';
   unsigned InN = fread(InBuf, 1, SigN, InF);
   int Status = 0;
   if (InN != SigN)
      Status = 1;
   else if (strcmp(InBuf, Signature))
      Status = 1;
   else if (fread(Buf, 1, 2, InF) != 2)
      Status = 1;
   else {
      Address = (Buf[1] << 8) | Buf[0];
      InN = SigN + 2;
   }
   if (fseek(InF, 0, SEEK_END))
      Status = 2;
   else if ((SigN = ftell(InF)) < InN)
      Status = 2;
   else
      Size = SigN - InN;
   if (fseek(InF, InN, SEEK_SET))
      Status = 3;
   return Status;
}

static bool LoadBin(char *InFile, uint32_t Offset) {
   FILE *InF = fopen(InFile, "rb");
   if (!InF)
      return false;
   uint32_t Size;
   if (strlen(InFile) > 4 && !strcmp(InFile + strlen(InFile) - 4, ".hex")) {
      struct HexQ Qb;
      HexInBeg(&Qb);
      char Buf[0x100];
      while (fgets(Buf, sizeof(Buf), InF))
         HexGet(&Qb, Buf, strlen(Buf));
      HexInEnd(&Qb);
      fclose(InF);
      return true;
   } else if (strlen(InFile) > 4 && !strcmp(InFile + strlen(InFile) - 4, ".z80")) {
      int Status = GetHeader(InF, Offset, Size);
      if (Status) {
         fprintf(stderr, "Error %d reading z80 file \"%s\"\n", Status, InFile);
         return false;
      }
   } else { // bin file
      fseek(InF, 0, SEEK_END);
      Size = ftell(InF);
      fseek(InF, 0, SEEK_SET);
   }
   if (Size < 1 || Size > CodeMax - Offset) {
      fprintf(stderr, "File size (%u bytes) exceeds available RAM size (%u bytes)\n", Size, CodeMax - Offset);
      fclose(InF);
      return false;
   }
   if (Size != (unsigned)fread(Code + Offset, 1, (size_t)Size, InF)) {
      fprintf(stderr, "Cannot read file: \"%s\"\n", InFile);
      fclose(InF);
      return false;
   }
   LoRAM = Offset;
   HiRAM = Offset + Size - 1;
   Log(2, "Loaded %d data bytes from \"%s\" into RAM region [0x%04X...0x%04X]\n", Size, InFile, LoRAM, HiRAM);
   fclose(InF);
   return true;
}

// read, parse, disassemble and output
int main(int AC, char *AV[]) {
   char *InFile = 0, *ExFile = 0;
   bool DoHex = false;
   bool DoParse = false;
   bool DoParseInt = false;
   fprintf(stderr, "DasZ80 - small disassembler for Z80 code\n");
   fprintf(stderr, "Based on TurboDis Z80 by Markus Fritze\n");
   uint32_t Offset = 0;
   uint32_t Start = 0;
   int Fill = 0;
   for (int A = 1, Ax = 0; A < AC; A++) {
      if ('-' == AV[A][0]) {
         switch (AV[A][++Ax]) {
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
            case 'o': { // offset
               int InN = 0;
               if (AV[A][++Ax]) // "-oXXXX"
                  InN = sscanf(AV[A] + Ax, "%x", &Offset);
               else if (A < AC - 1) // "-o XXXX"
                  InN = sscanf(AV[++A], "%x", &Offset);
               if (InN)
                  Offset &= 0xffff; // limit to 64K
               else {
                  fprintf(stderr, "Error: option -o needs a hexadecimal argument\n");
                  return 1;
               }
               Ax = 0; // end of this arg group
            }
            break;
            case 's': { // start
               int InN = 0;
               if (AV[A][++Ax]) // "-sXXXX"
                  InN = sscanf(AV[A] + Ax, "%x", &Start);
               else if (A < AC - 1) // "-s XXXX"
                  InN = sscanf(AV[++A], "%x", &Start);
               if (InN)
                  Start &= 0xffff; // limit to 64K
               else {
                  fprintf(stderr, "Error: option -s needs a hexadecimal argument\n");
                  return 1;
               }
               Ax = 0; // end of this arg group
            }
            break;
            case 'p': // parse program flow
               DoParse = true;
            break;
            case 'r': // parse program flow
               DoParseInt = true;
            break;
            case 'v':
               ++Loudness;
            break;
            case 'x':
               DoHex = true;
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
         else if (!ExFile)
            ExFile = AV[A];
         else {
            Usage(AV[0]);
            return 1;
         } // check next arg string
      }
   }
   memset(Code, Fill, sizeof(Code));
   memset(Mode, Empty, sizeof(Mode));
   if (!LoadBin(InFile, Offset))
      return 1;
   uint32_t CodeN = HiRAM + 1 - LoRAM;
   Offset = LoRAM;
   CodeN += Offset;
   if (DoParse) {
      for (uint32_t IP = LoRAM; IP <= HiRAM; IP++) // all data
         Mode[IP] = Data;
      if (DoParseInt) {
         for (int n = 0; n < 0100; n += 010)
            if ((Mode[n]&0x0f) == Data)
               ParseOpcodes(n); // parse RST vectors (if needed)
         if ((Mode[0146]&0x0f) == Data)
            ParseOpcodes(0146); // parse also NMI vector (if needed)
      }
      ParseOpcodes(LoRAM);
   }
   FILE *ExF = ExFile? fopen(ExFile, "w"): stdout;
   if (!ExF) {
      fprintf(stderr, "Error: cannot open outfile \"%s\"\n", ExFile);
      return 1;
   }
   uint32_t IP = Start >= Offset? Start: Offset;
   fprintf(ExF, "        ORG     $%04X\n", IP);
   while (IP <= HiRAM) {
      if ((Mode[IP]&0x0f) == Data) {
         fprintf(ExF, "L%4.4X:  DEFB", (uint16_t)IP);
         uint32_t n;
         for (n = 0; n < 16; n++) {
            if ((Mode[IP + n]&0x0f) != Data || IP + n >= CodeN)
               break;
            fprintf(ExF, "%s$%2.2X", (n)? ",": "    ", Code[IP + n]);
         }
         fprintf(ExF, "\n");
         IP += n;
      } else {
         uint32_t N = OpLen(IP); // get length of opcode
         if (!DoHex) {
            if (Mode[IP]&0x10)
               fprintf(ExF, "L%4.4X:  ", IP);
            else
               fprintf(ExF, "        ");
         } else {
            fprintf(ExF, "%4.4X    ", (uint16_t)IP);
            for (uint32_t n = 0; n < N; n++)
               fprintf(ExF, "%2.2X ", Code[IP + n]);
            for (uint32_t n = 4; n > N; n--)
               fprintf(ExF, "   ");
            fprintf(ExF, "    ");
         }
         char Line[0x100]; // output string
         Disassemble(IP, Line, sizeof(Line));
         fprintf(ExF, "%s\n", Line);
         IP += N;
      }
   }
   fclose(ExF);
}

// callback from HexIn.c when data has arrived
HexBool HexGetData(struct HexQ *Qh, HexRecordT Type, HexBool Error) {
   static uint32_t HexDataN = 0;
   Error = Error || (Qh->Length < Qh->LineN);
   if (Type == HexLineRec && !Error) {
      Log(4, "IHEX addr: $%04X, data len: %d\n", HexAddress(Qh), Qh->Length);
      memcpy(Code + HexAddress(Qh), Qh->Line, Qh->Length);
      if (HexAddress(Qh) < LoRAM)
         LoRAM = HexAddress(Qh);
      if (HexAddress(Qh) + Qh->Length >= HiRAM)
         HiRAM = HexAddress(Qh) + Qh->Length - 1;
      HexDataN += Qh->Length;
   } else if (Type == HexEndRec) {
      Log(4, "IHEX EOF\n");
      Log(1, "Loaded %d data bytes from hexfile into RAM region [0x%04X...0x%04X]\n", HexDataN, LoRAM, HiRAM);
      if (HexDataN != HiRAM + 1 - LoRAM)
         Log(1, "(size: %d Bytes)\n", HiRAM + 1 - LoRAM);
      else
         Log(1, "");
   }
   return !Error;
}
