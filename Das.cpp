// DasZ80 Disassembler
// ───────────────────
// The program has two parts:
// ▪	Analyze the code.
// 	The disassembler tries to analyze what part of the binary data is program code and what part is data.
// 	It start with all hardware vectors of the Z80 (‟RST” opcodes, ‟NMI”) and parses all jumps via a recursive analyze via ‟OpScan()”.
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
// 		If you find a jump table - like on Markuz' Futura aquarium computer - more entry points for ‟OpScan()” will need to be added.
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
static uint8_t Code[CodeMax];	// Memory for the code.
static uint8_t Mode[CodeMax];	// Type of opcode.
// Values for 'Mode': 0x10: (Bit 4) indicates a jump/call address reference.
enum { Empty, Opcode, Operand, Data };

static uint32_t LoRAM = CodeMax, HiRAM = 0;

static char NumPre = '$';

static inline uint8_t GetB(uint16_t &IP) { return Code[IP++]; }
static inline int16_t GetDs(uint16_t &IP) {
   int16_t Ds = Code[IP++];
   return Ds >= 0x80? Ds - 0x100: Ds;
}
static inline uint16_t GetJs(uint16_t &IP) {
   uint16_t Js = Code[IP++];
   return Js >= 0x80? IP + Js - 0x100: IP + Js;
}
static inline uint16_t GetW(uint16_t &IP) {
   uint16_t L = Code[IP++], H = Code[IP++]; return L | H << 8;
}

// Calculate the length of an opcode.
static int OpLen(uint16_t IP) {
   uint16_t IP0 = IP;
MainOp:
   switch (GetB(IP)) { // The primary opcode.
   // ld Rd,Db; [Rd: B; C; D; E; H; L; (HL); A]
      case 0006: case 0016: case 0026: case 0036: case 0046: case 0056: case 0066: case 0076:
   // djnz Js; jr Js; jr nz,Js; jr z,Js; jr nc,Js; jr c,Js
      case 0020: case 0030: case 0040: case 0050: case 0060: case 0070:
   // AOp Db; [AOp: add A,; adc A,; sub; sbc A,; and; xor; or; cp]
      case 0306: case 0316: case 0326: case 0336: case 0346: case 0356: case 0366: case 0376:
   // out (Pb),A; in A,(Pb)
      case 0323: case 0333:
   // Shift-,Rotate-,Bit-opcodes.
      case 0313:
         IP++;
      break;
   // ld BC,Dw; ld DE,Dw; ld HL,Dw; ld SP,(Dw)
      case 0001: case 0021: case 0041: case 0061:
   // ld (Aw),HL; ld HL,(Aw); ld (Aw),A; ld A,(Aw)
      case 0042: case 0052: case 0062: case 0072:
   // jp Cc,Js; [Cc: nz; z; nc; c; po; pe; p; m]
      case 0302: case 0312: case 0322: case 0332: case 0342: case 0352: case 0362: case 0372:
   // jp Aw; call Aw
      case 0303: case 0315:
   // call Cc,Js; [Cc: nz; z; nc; c; po; pe; p; m]
      case 0304: case 0314: case 0324: case 0334: case 0344: case 0354: case 0364: case 0374:
         IP += 2;
      break;
   // Indexed Operations [Rx: IX; IY].
      case 0335: case 0375: switch (GetB(IP)) { // The secondary opcode.
      // IOp (Rx+Ds); [IOp: inc; dec]
         case 0064: case 0065:
      // ld Rd,(Rx+Ds); [Rd: B; C; D; E; H; L; A]
         case 0106: case 0116: case 0126: case 0136: case 0146: case 0156: case 0176:
      // ld (Rx+Ds),Rs; [Rs: B; C; D; E; H; L; A]
         case 0160: case 0161: case 0162: case 0163: case 0164: case 0165: case 0167:
      // AOp (Rx+Ds); [AOp: add A,; adc A,; sub A,; sbc A,; and; xor; or; cp]
         case 0206: case 0216: case 0226: case 0236: case 0246: case 0256: case 0266: case 0276:
            IP++;
         break;
      // ld (Rx+Ds),Db
         case 0066:
      // Rotation,Bit-Op (Rx+Ds).
         case 0313:
            IP += 2;
         break;
      // All other cases, both official and unofficial, match the main opcode table.
         default: IP--; goto MainOp;
      }
      break;
      case 0355: switch (GetB(IP)) { // The secondary opcode.
      // ld (Aw),BC; ld (Aw),DE; ld (Aw),HL; ld (Aw),SP
         case 0103: case 0123: case 0143: case 0163:
      // ld BC,(Aw); ld DE,(Aw); ld HL,(Aw); ld SP,(Aw)
         case 0113: case 0133: case 0153: case 0173:
            IP += 2;
         break;
      }
      break;
   }
   return IP - IP0;
}

static void OpScan(uint16_t IP) {
   bool Label = true;
   while (true) {
   // Mark address references to the opcode area.
      if (Label) Mode[IP] |= 0x10;
   // Break out upon reaching an already-processed code area.
      if ((Mode[IP]&0x0f) == Opcode) break;
   // Abort upon finding an operator/operand collision; i.e. overlapping opcode areas.
      if ((Mode[IP]&0x0f) == Operand) { printf("Illegal jump at addr %4.4XH\n", IP); return; }
   // Mark the opcode area as an operator followed by operands.
      Mode[IP] = Opcode;
      int N = OpLen(IP);
      for (int16_t n = 1; n < N; n++) Mode[IP + n] = Operand;
   // Mark address references to the opcode area.
      if (Label) Mode[IP] |= 0x10, Label = false;
      uint32_t IP0 = IP, NextIP = IP0 + N; // Save the next opcode.
      uint8_t Op;
   MainOp:
      switch (Op = GetB(IP)) { // Fetch the primary opcode.
      // jp Cc,Aw; [Cc: nz; z; nc; c; po; pe; p; m]
         case 0302: case 0312: case 0322: case 0332: case 0342: case 0352: case 0362: case 0372: OpScan(GetW(IP)); break;
      // jr Cc,Js; [Cc: nz; z; nc; c]
         case 0040: case 0050: case 0060: case 0070: OpScan(GetJs(IP)); break;
      // call Cc,Aw; [Cc: nz; z; nc; c; po; pe; p; m]
         case 0304: case 0314: case 0324: case 0334: case 0344: case 0354: case 0364: case 0374: OpScan(GetW(IP)); break;
      // rst 10q*n; [n: 0; 1; 2; 3; 4; 5; 6; 7]
         case 0307: case 0317: case 0327: case 0337: case 0347: case 0357: case 0367: case 0377: OpScan(Op&070); break;
      // djnz Js
         case 0020: OpScan(GetJs(IP)); break;
      // jp Aw
         case 0303: NextIP = GetW(IP), Label = true; break;
      // jr Js
         case 0030: NextIP = GetJs(IP), Label = true; break;
      // call Aw
         case 0315: OpScan(GetW(IP)); break;
      // ret
         case 0311: return;
#if DETECT_JUMPTABLES
      // jp (HL), jp (IX) if prefixed by 0335 or jp (IY) if prefixed by 0375.
         case 0351: puts("JP (...) found"), exit(-1);
#else
         case 0351: return;
#endif
         case 0335: case 0375: goto MainOp;
      // retn; reti
         case 0355: switch (Op = GetB(IP)) {
            case 0105: case 0115: return;
         }
         break;
      }
      IP = NextIP;
   }
}

static void _Gen(char *Buf, size_t BufN, const char *Format, ...) {
   va_list AP; va_start(AP, Format), vsnprintf(Buf, BufN, Format, AP), va_end(AP);
}
#define Gen(...) _Gen(Buf, BufN, __VA_ARGS__)

// Disassemble.
static void Disassemble(uint16_t IP, char *Buf, size_t BufN) {
   uint8_t Op = GetB(IP), X = (Op >> 6)&3, Y = (Op >> 3)&7, Z = Op&7, Yq;
   static const char *Rb[8] = { "B", "C", "D", "E", "H", "L", "(HL)", "A" };
   static const char *Rw[4] = { "BC", "DE", "HL", "SP" };
   static const char *Cc[8] = { "NZ", "Z", "NC", "C", "PO", "PE", "P", "M" };
   static const char *AOp[8] = { "ADD     A,", "ADC     A,", "SUB", "SBC     A,", "AND", "XOR", "OR", "CP" };
   static const char *ShOp[8] = { "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL" };
   static const char *S0Op[8] = { "RLCA", "RRCA", "RLA", "RRA", "DAA", "CPL", "SCF", "CCF" };
   static const char *BOp[4][4] = {
      "LDI", "LDD", "LDIR", "LDDR",
      "CPI", "CPD", "CPIR", "CPDR",
      "INI", "IND", "INIR", "INDR",
      "OUTI", "OUTD", "OTIR", "OTDR",
   };
   const char *Rx; // Temporary index register string.
MainOp:
   switch (X) {
      case 0: switch (Z) {
         case 0: switch (Y) {
            case 0: Gen("NOP"); break;
            case 1: Gen("EX      AF,AF'"); break;
            case 2: Gen("DJNZ    %c%4.4X", NumPre, GetJs(IP)); break;
            case 3: Gen("JR      %c%4.4X", NumPre, GetJs(IP)); break;
            default: Gen("JR      %s,%c%4.4X", Cc[Y&3], NumPre, GetJs(IP)); break;
         }
         break;
         case 1: switch (Yq = Y >> 1, Y&1) {
            case 0: Gen("LD      %s,$%4.4X", Rw[Yq], GetW(IP)); break;
            case 1: Gen("ADD     HL,%s", Rw[Yq]); break;
         }
         break;
         case 2: switch (Y) {
            case 0: Gen("LD      (BC),A"); break;
            case 1: Gen("LD      A,(BC)"); break;
            case 2: Gen("LD      (DE),A"); break;
            case 3: Gen("LD      A,(DE)"); break;
            case 4: Gen("LD      ($%4.4X),HL", GetW(IP)); break;
            case 5: Gen("LD      HL,($%4.4X)", GetW(IP)); break;
            case 6: Gen("LD      ($%4.4X),A", GetW(IP)); break;
            case 7: Gen("LD      A,($%4.4X)", GetW(IP)); break;
         }
         break;
         case 3: switch (Yq = Y >> 1, Y&1) {
            case 0: Gen("INC     %s", Rw[Yq]); break;
            case 1: Gen("DEC     %s", Rw[Yq]); break;
         }
         break;
         case 4: Gen("INC     %s", Rb[Y]); break;
         case 5: Gen("DEC     %s", Rb[Y]); break;
         case 6: Gen("LD      %s,$%2.2X", Rb[Y], GetB(IP)); break;
         case 7: Gen("%s", S0Op[Y]); break;
      }
      break;
   // ld Rd,Rs or halt.
      case 1: switch (Op&077) {
      // Order 66.
         case 066: Gen("HALT"); break;
         default: Gen("LD      %s,%s", Rb[Y], Rb[Z]); break;
      }
      break;
      case 2: Gen("%-8s%s", AOp[Y], Rb[Z]); break;
      case 3: switch (Z) {
         case 0: Gen("RET     %s", Cc[Y]); break;
         case 1: switch (Yq = Y >> 1, Y&1) {
            case 0: Gen("POP     %s", Yq == 3? "AF": Rw[Yq]); break;
            case 1: switch (Yq) {
               case 0: Gen("RET"); break;
               case 1: Gen("EXX"); break;
               case 2: Gen("JP      (HL)"); break;
               case 3: Gen("LD      SP,HL"); break;
            }
         }
         break;
         case 2: Gen("JP      %s,%c%4.4X", Cc[Y], NumPre, GetW(IP)); break;
         case 3: switch (Y) {
            case 0: Gen("JP      %c%4.4X", NumPre, GetW(IP)); break;
         // 0313
            case 1:
               Op = GetB(IP), X = (Op >> 6)&3, Y = (Op >> 3)&7, Z = Op&7; // Fetch the secondary opcode.
               switch (X) {
                  case 0: Gen("%-8s%s", ShOp[Y], Rb[Z]); break;
                  case 1: Gen("BIT     %d,%s", Y, Rb[Z]); break;
                  case 2: Gen("RES     %d,%s", Y, Rb[Z]); break;
                  case 3: Gen("SET     %d,%s", Y, Rb[Z]); break;
               }
            break;
            case 2: Gen("OUT     ($%2.2X),A", GetB(IP)); break;
            case 3: Gen("IN      A,($%2.2X)", GetB(IP)); break;
            case 4: Gen("EX      (SP),HL"); break;
            case 5: Gen("EX      DE,HL"); break;
            case 6: Gen("DI"); break;
            case 7: Gen("EI"); break;
         }
         break;
         case 4: Gen("CALL    %s,%c%4.4X", Cc[Y], NumPre, GetW(IP)); break;
         case 5: switch (Yq = Y >> 1, Y&1) {
            case 0: Gen("PUSH    %s", Yq == 3? "AF": Rw[Yq]); break;
            case 1: switch (Yq) {
               case 0: Gen("CALL    %c%4.4X", NumPre, GetW(IP)); break;
            // 0355
               case 2:
                  Op = GetB(IP), X = (Op >> 6)&3, Y = (Op >> 3)&7, Z = Op&7; // Fetch the secondary opcode.
                  switch (X) {
                  // Undocumented: "nop".
                     case 0: case 3: UnDocNop: Gen("NOP"); break;
                     case 1: switch (Z) {
                        case 0: switch (Y) {
                        // Undocumented: "in (C)" set flags but do not modify a reg.
                           case 6: Gen("IN      (C)"); break;
                           default: Gen("IN      %s,(C)", Rb[Y]); break;
                        }
                        break;
                        case 1: switch (Y) {
                        // Undocumented: "out (C), 0" output 0.
                           case 6: Gen("OUT     (C),0"); break;
                           default: Gen("OUT     (C),%s", Rb[Y]); break;
                        }
                        break;
                        case 2: switch (Yq = Y >> 1, Y&1) {
                           case 0: Gen("SBC     HL,%s", Rw[Yq]); break;
                           case 1: Gen("ADC     HL,%s", Rw[Yq]); break;
                        }
                        break;
                        case 3: switch (Yq = Y >> 1, Y&1) {
                           case 0: Gen("LD      ($%4.4X),%s", GetW(IP), Rw[Yq]); break;
                           case 1: Gen("LD      %s,($%4.4X)", Rw[Yq], GetW(IP)); break;
                        }
                        break;
                        case 4: switch (Y) {
                        // Undocumented: the default case is also "neg".
                           default:
                           case 0: Gen("NEG"); break;
                        }
                        break;
                        case 5: switch (Y) {
                        // Undocumented: the default case is also "retn".
                           default:
                           case 0: Gen("RETN"); break;
                           case 1: Gen("RETI"); break;
                        }
                        break;
                        case 6: switch (Y&3) {
                           case 0: Gen("IM      0"); break;
                           case 1: Gen("IM      0/1"); break;
                           case 2: Gen("IM      1"); break;
                           case 3: Gen("IM      2"); break;
                        }
                        break;
                        case 7: switch (Y) {
                           case 0: Gen("LD      I,A"); break;
                           case 1: Gen("LD      R,A"); break;
                           case 2: Gen("LD      A,I"); break;
                           case 3: Gen("LD      A,R"); break;
                           case 4: Gen("RRD"); break;
                           case 5: Gen("RLD"); break;
                           default: goto UnDocNop;
                        }
                        break;
                     }
                     break;
                     case 2: switch (Z) {
                        case 0: case 1: case 2: case 3: switch (Y) {
                           default: goto UnDocNop;
                           case 4: case 5: case 6: case 7: Gen("%s", BOp[Z][Y&3]); break;
                        }
                        break;
                        default: goto UnDocNop;
                     }
                     break;
                  }
               break;
            // 335: IX, 375: IY.
               case 1: case 3: {
                  int16_t Ds; // Used for signed address indexes.
                  Rx = (Yq&2)? "IY": "IX";
                  Op = GetB(IP), Y = (Op >> 3)&7, Z = Op&7; // Fetch the secondary opcode.
                  switch (Op) {
                     default: goto MainOp;
                     case 0011: Gen("ADD     %s,BC", Rx); break;
                     case 0031: Gen("ADD     %s,DE", Rx); break;
                     case 0051: Gen("ADD     %s,%s", Rx, Rx); break;
                     case 0071: Gen("ADD     %s,SP", Rx); break;
                     case 0041: Gen("LD      %s,$%4.4X", Rx, GetW(IP)); break;
                     case 0042: Gen("LD      ($%4.4X),%s", GetW(IP), Rx); break;
                     case 0052: Gen("LD      %s,($%4.4X)", Rx, GetW(IP)); break;
                     case 0043: Gen("INC     %s", Rx); break;
                     case 0053: Gen("DEC     %s", Rx); break;
                     case 0044: Gen("INC     %sH", Rx); break;
                     case 0045: Gen("DEC     %sH", Rx); break;
                     case 0046: Gen("LD      %sH,$%2.2X", Rx, GetB(IP)); break;
                     case 0054: Gen("INC     %sL", Rx); break;
                     case 0055: Gen("DEC     %sL", Rx); break;
                     case 0056: Gen("LD      %sL,$%2.2X", Rx, GetB(IP)); break;
                     case 0064: Ds = GetDs(IP), Ds >= 0? Gen("INC     (%s+$%2.2X)", Rx, Ds): Gen("INC     (%s-$%2.2X)", Rx, -Ds); break;
                     case 0065: Ds = GetDs(IP), Ds >= 0? Gen("DEC     (%s+$%2.2X)", Rx, Ds): Gen("DEC     (%s-$%2.2X)", Rx, -Ds); break;
                     case 0066: Ds = GetDs(IP), Ds >= 0? Gen("LD      (%s+$%2.2X),$%2.2X", Rx, Ds, GetB(IP)): Gen("LD      (%s-$%2.2X),$%2.2X", Rx, -Ds, GetB(IP)); break;
                     case 0144: case 0154: Gen("LD      %s%s,%sH", Rx, Rb[Y], Rx); break;
                     case 0145: case 0155: Gen("LD      %s%s,%sL", Rx, Rb[Y], Rx); break;
                     case 0104: case 0114: case 0124: case 0134: case 0174: Gen("LD      %s,%sH", Rb[Y], Rx); break;
                     case 0105: case 0115: case 0125: case 0135: case 0175: Gen("LD      %s,%sL", Rb[Y], Rx); break;
                     case 0106: case 0116: case 0126: case 0136: case 0146: case 0156: case 0176:
                        Ds = GetDs(IP), Ds >= 0? Gen("LD      %s,(%s+$%2.2X)", Rb[Y], Rx, Ds): Gen("LD      %s,(%s-$%2.2X)", Rb[Y], Rx, -Ds);
                     break;
                     case 0140: case 0141: case 0142: case 0143: case 0147: Gen("LD      %sH,%s", Rx, Rb[Y]); break;
                     case 0150: case 0151: case 0152: case 0153: case 0157: Gen("LD      %sL,%s", Rx, Rb[Y]); break;
                     case 0160: case 0161: case 0162: case 0163: case 0164: case 0165: case 0167:
                        Ds = GetDs(IP), Ds >= 0? Gen("LD      (%s+$%2.2X),%s", Rx, Ds, Rb[Z]): Gen("LD      (%s-$%2.2X),%s", Rx, -Ds, Rb[Z]);
                     break;
                     case 0204: case 0214: case 0224: case 0234: case 0244: case 0254: case 0264: case 0274: Gen("%-8s%sH", AOp[Y], Rx); break;
                     case 0205: case 0215: case 0225: case 0235: case 0245: case 0255: case 0265: case 0275: Gen("%-8s%sL", AOp[Y], Rx); break;
                     case 0206: case 0216: case 0226: case 0236: case 0246: case 0256: case 0266: case 0276:
                        Ds = GetDs(IP), Ds >= 0? Gen("%-8s(%s+$%2.2X)", AOp[Y], Rx, Ds): Gen("%-8s(%s-$%2.2X)", AOp[Y], Rx, -Ds);
                     break;
                     case 0341: Gen("POP     %s", Rx); break;
                     case 0343: Gen("EX      (SP),%s", Rx); break;
                     case 0345: Gen("PUSH    %s", Rx); break;
                     case 0351: Gen("JP      (%s)", Rx); break;
                     case 0371: Gen("LD      SP,%s", Rx); break;
                     case 0313:
                        Ds = GetDs(IP);
                        Op = GetB(IP), X = (Op >> 6)&3, Y = (Op >> 3)&7, Z = Op&7; // Fetch the tertiary opcode.
                        switch (Z) {
                           default: switch (X) {
                              case 0: Ds >= 0? Gen("%-8s(%s+$%2.2X),%s", ShOp[Y], Rx, Ds, Rb[X]): Gen("%-8s(%s-$%2.2X),%s", ShOp[Y], Rx, -Ds, Rb[X]); break;
                              case 1: Ds >= 0? Gen("BIT     %d,(%s+$%2.2X),%s", Y, Rx, Ds, Rb[X]): Gen("BIT     %d,(%s-$%2.2X),%s", Y, Rx, -Ds, Rb[X]); break;
                              case 2: Ds >= 0? Gen("RES     %d,(%s+$%2.2X),%s", Y, Rx, Ds, Rb[X]): Gen("RES     %d,(%s-$%2.2X),%s", Y, Rx, -Ds, Rb[X]); break;
                              case 3: Ds >= 0? Gen("SET     %d,(%s+$%2.2X),%s", Y, Rx, Ds, Rb[X]): Gen("SET     %d,(%s-$%2.2X),%s", Y, Rx, -Ds, Rb[X]); break;
                           }
                           break;
                           case 6: switch (X) {
                              case 0: Ds >= 0? Gen("%-8s(%s+$%2.2X)", ShOp[Y], Rx, Ds): Gen("%-8s(%s-$%2.2X)", ShOp[Y], Rx, -Ds); break;
                              case 1: Ds >= 0? Gen("BIT     %d,(%s+$%2.2X)", Y, Rx, Ds): Gen("BIT     %d,(%s-$%2.2X)", Y, Rx, -Ds); break;
                              case 2: Ds >= 0? Gen("RES     %d,(%s+$%2.2X)", Y, Rx, Ds): Gen("RES     %d,(%s-$%2.2X)", Y, Rx, -Ds); break;
                              case 3: Ds >= 0? Gen("SET     %d,(%s+$%2.2X)", Y, Rx, Ds): Gen("SET     %d,(%s-$%2.2X)", Y, Rx, -Ds); break;
                           }
                           break;
                        }
                     break;
                  }
               }
               break;
            }
            break;
         }
         break;
         case 6: Gen("%-8s$%2.2X", AOp[Y], GetB(IP)); break;
         case 7: Gen("RST     $%2.2X", Op&070); break;
      }
      break;
   }
}

static void Usage(const char *Path) {
   const char *App = Path;
   for (char Ch; (Ch = *Path++) != '\0'; ) if (Ch == '/' || Ch == '\\') App = Path;
   printf(
      "Usage:\n"
      "  %s [-fXX] [-oXXXX] [-p] [-r] [-x] <InFile> [<OutFile>]\n"
      "    -fXX    fill unused memory, XX = 0x00 .. 0xff\n"
      "    -oXXXX  org XXXX = 0x0000 .. 0xffff\n"
      "    -p      parse program flow\n"
      "    -r      parse also rst and nmi\n"
      "    -x      show hexdump\n",
      App
   );
}

// The Z80 format is used by the Z80-asm.
// http://wwwhomes.uni-bielefeld.de/achim/z80-asm.html
// *.z80 files are bin files with a header telling the bin offset
//	struct z80_header {
//		const char *Signature = "Z80ASM" "\032" "\n";
//		uint16_t Offset;
//	}
// Read the header of a file and test if it's a Z80 ASM file, then read the address.
// Return value: 0 = OK, 1 = this is not a Z80 asm file, 2, 3 = seek malfunction.
static int GetHeader(FILE *InF, uint32_t &Address, uint32_t &Size) {
   const char *Signature = "Z80ASM" "\032" "\n";
   unsigned char Buf[2];
   unsigned SigN = strlen(Signature);
   char InBuf[9]; InBuf[SigN] = '\0';
   unsigned InN = fread(InBuf, 1, SigN, InF);
   int Status = 0;
   if (InN != SigN) Status = 1;
   else if (strcmp(InBuf, Signature) != 0) Status = 1;
   else if (fread(Buf, 1, 2, InF) != 2) Status = 1;
   else Address = (Buf[1] << 8) | Buf[0], InN = SigN + 2;
   if (fseek(InF, 0, SEEK_END)) Status = 2;
   else if ((SigN = ftell(InF)) < InN) Status = 2;
   else Size = SigN - InN;
   if (fseek(InF, InN, SEEK_SET)) Status = 3;
   return Status;
}

static bool LoadBin(char *InFile, uint32_t Offset) {
   bool Ok = false;
   FILE *InF = fopen(InFile, "rb"); if (InF == nullptr) return Ok;
   uint32_t Size;
   if (strlen(InFile) > 4 && strcmp(InFile + strlen(InFile) - 4, ".hex") == 0) {
      struct HexIn Q;
      char Buf[0x100];
      while (fgets(Buf, sizeof Buf, InF)) Q.Get(Buf, strlen(Buf));
      goto End2;
   } else if (strlen(InFile) > 4 && strcmp(InFile + strlen(InFile) - 4, ".z80") == 0) {
      int Status = GetHeader(InF, Offset, Size);
      if (Status != 0) {
         fprintf(stderr, "Error %d reading z80 file \"%s\"\n", Status, InFile);
         goto End1;
      }
   } else fseek(InF, 0, SEEK_END), Size = ftell(InF), fseek(InF, 0, SEEK_SET); // bin file.
   if (Size < 1 || Size > CodeMax - Offset) {
      fprintf(stderr, "File size (%u bytes) exceeds available RAM size (%u bytes)\n", Size, CodeMax - Offset);
      goto End1;
   } else if (Size != (unsigned)fread(Code + Offset, 1, (size_t)Size, InF)) {
      fprintf(stderr, "Cannot read file: \"%s\"\n", InFile);
      goto End1;
   }
   LoRAM = Offset, HiRAM = Offset + Size - 1;
End2:
   Ok = true;
End1:
   fclose(InF);
   return Ok;
}

// Read, parse, disassemble and output.
int main(int AC, char *AV[]) {
   char *InFile = 0, *ExFile = 0;
   bool DoHex = false, DoParse = false, DoParseInt = false;
   fprintf(stderr, "DasZ80 - small disassembler for Z80 code\n");
   fprintf(stderr, "Based on TurboDis Z80 by Markus Fritze\n");
   uint32_t Offset = 0, Start = 0;
   int Fill = 0;
   for (int A = 1, Ax = 0; A < AC; A++)
      if (AV[A][0] == '-') {
         switch (AV[A][++Ax]) {
         // Fill.
            case 'f': {
               int InN = 0;
            // "-fXX"
               if (AV[A][++Ax] != '\0') InN = sscanf(AV[A] + Ax, "%x", &Fill);
            // "-f XX"
               else if (A < AC - 1) InN = sscanf(AV[++A], "%x", &Fill);
               if (InN > 0) Fill &= 0x00ff; // Limit to byte size.
               else {
                  fprintf(stderr, "Error: option -f needs a hexadecimal argument\n");
                  return 1;
               }
               Ax = 0; // The end of this arg group.
            }
            break;
         // Offset.
            case 'o': {
               int InN = 0;
            // "-oXXXX"
               if (AV[A][++Ax] != '\0') InN = sscanf(AV[A] + Ax, "%x", &Offset);
            // "-o XXXX"
               else if (A < AC - 1) InN = sscanf(AV[++A], "%x", &Offset);
               if (InN > 0) Offset &= 0xffff; // Limit to 64K.
               else {
                  fprintf(stderr, "Error: option -o needs a hexadecimal argument\n");
                  return 1;
               }
               Ax = 0; // The end of this arg group.
            }
            break;
         // Start.
            case 's': {
               int InN = 0;
            // "-sXXXX"
               if (AV[A][++Ax] != '\0') InN = sscanf(AV[A] + Ax, "%x", &Start);
            // "-s XXXX"
               else if (A < AC - 1) InN = sscanf(AV[++A], "%x", &Start);
               if (InN > 0) Start &= 0xffff; // Limit to 64K.
               else {
                  fprintf(stderr, "Error: option -s needs a hexadecimal argument\n");
                  return 1;
               }
               Ax = 0; // The end of this arg group.
            }
            break;
         // Parse the program flow.
            case 'p': DoParse = true, NumPre = 'L'; break;
         // Parse the program flow.
            case 'r': DoParseInt = true; break;
            case 'x': DoHex = true; break;
            default: Usage(AV[0]); return 1;
         }
      // If one more arg char, keep this arg group.
         if (Ax > 0 && AV[A][Ax + 1] != '\0') { A--; continue; }
         Ax = 0; // Start from the beginning in the next arg group.
      } else if (InFile == nullptr) InFile = AV[A];
      else if (ExFile == nullptr) ExFile = AV[A];
   // Check the next arg string.
      else { Usage(AV[0]); return 1; }
   memset(Code, Fill, sizeof Code);
   memset(Mode, Empty, sizeof Mode);
   if (!LoadBin(InFile, Offset)) return 1;
   Offset = LoRAM;
   uint32_t CodeN = HiRAM + 1;
   if (DoParse) {
   // All data, by starting default.
      for (uint32_t IP = LoRAM; IP <= HiRAM; IP++) Mode[IP] = Data;
      if (DoParseInt) {
      // Parse the rst vectors, if needed.
         for (int IP = 0; IP < 0100; IP += 010) if ((Mode[IP]&0x0f) == Data) OpScan(IP);
      // Also, parse the NMI vector, if needed.
         if ((Mode[0146]&0x0f) == Data) OpScan(0146);
      }
      OpScan(LoRAM);
   }
   FILE *ExF = ExFile? fopen(ExFile, "w"): stdout;
   if (ExF == nullptr) {
      fprintf(stderr, "Error: cannot open outfile \"%s\"\n", ExFile);
      return 1;
   }
   uint32_t IP = Start >= Offset? Start: Offset;
   fprintf(ExF, "        ORG     $%04X\n", IP);
   while (IP <= HiRAM)
      if ((Mode[IP]&0x0f) == Data) {
         fprintf(ExF, "L%4.4X:  DEFB", (uint16_t)IP);
         for (uint32_t n = 0; n < 16; IP++, n++) {
            if ((Mode[IP]&0x0f) != Data || IP >= CodeN) break;
            fprintf(ExF, "%s$%2.2X", n? ",": "    ", Code[IP]);
         }
         fprintf(ExF, "\n");
      } else {
         uint32_t N = OpLen(IP); // Get the opcode length.
         if (!DoHex) {
            if (Mode[IP]&0x10) fprintf(ExF, "%c%4.4X:  ", DoParse? 'L': '$', IP);
            else fprintf(ExF, "        ");
         } else {
            fprintf(ExF, "%c%4.4X   ", DoParse? 'L': '$', (uint16_t)IP);
            for (uint32_t n = 0; n < N; n++) fprintf(ExF, "%2.2X ", Code[IP + n]);
            for (uint32_t n = 4; n > N; n--) fprintf(ExF, "   ");
            fprintf(ExF, "    ");
         }
         char Line[0x100]; // The output string.
         Disassemble(IP, Line, sizeof Line);
         fprintf(ExF, "%s\n", Line);
         IP += N;
      }
   fclose(ExF);
}

// Call-back from HexIn.cpp when data has arrived.
bool HexIn::GetData(HexRecordT Type, bool Error) {
   static uint32_t HexDataN = 0;
   Error = Error || _Length < _LineN;
   if (Type == HexLineRec && !Error) {
      memcpy(Code + HexAddress(), _Line, _Length);
      if (HexAddress() < LoRAM) LoRAM = HexAddress();
      if (HexAddress() + _Length >= HiRAM) HiRAM = HexAddress() + _Length - 1;
      HexDataN += _Length;
   }
   return !Error;
}
