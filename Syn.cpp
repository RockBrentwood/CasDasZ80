// Compile a tokenzied line.
#include "Cas.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

bool AtEnd = false;

// Get operands for an opcode.
static int16_t GetOperand(CommandP &Cmd, int32_t *ValueP) {
   LastPatch = nullptr; // To be safe: reset the patch entry.
   *ValueP = 0;
   int16_t Type = Cmd->Type, Value = Cmd++->Value; // Get a value and type.
   Log(2, "GetOperand( %d, %X, %d )\n", Type, Value, *ValueP);
   if (Type == OpL) {
      if (Value >= 0x300 && Value <= 0x4ff) {
      // AF': skip "'" and return AF'.
         if (Value == 0x323 && Cmd->Type == OpL && Cmd->Value == '\'') Cmd++, Value = 0x324;
      // Register or condition.
         return Value;
      }
      if (Value >= 0x100 && Value <= 0x2ff) Error("Illegal operand"); // An opcode.
      if (Value == '(') { // Indirect addressing?
         Type = Cmd->Type;
         int16_t SValue = Cmd++->Value; // Get a value and type.
         if (Type == OpL) {
            if ((SValue&0xff0) == 0x310 || SValue == 0x301) { // Register.
               Type = Cmd->Type, Value = Cmd++->Value; // Get a value and type.
               if (Type == OpL && Value == ')') {
                  if (SValue == 0x312) return 0x306; // (HL): to combine them easier.
                  return SValue + 0x200; // (C),(BC),(DE),(SP).
               }
               Error("Closing bracket missing after (BC,(DE,(HL or (SP");
            }
            if ((SValue&0xff0) == 0x330) { // IX,IY
               if (Cmd->Type == OpL) { // Does an operator follow?
                  Value = Cmd->Value;
                  if (Value == '+' || Value == '-') {
                     *ValueP = GetExp(Cmd);
                     Type = Cmd->Type, Value = Cmd++->Value; // Get a bracket.
                     if (Type == OpL && Value == ')') return SValue + 0x300; // (IX+Ds); (IY+Ds).
                     Error("Closing bracket missing after (IX or (IY");
                  } else {
                     if (Type == OpL && Value == ')') {
                        Cmd++; // Skip the bracket.
                        return SValue + 0x200; // (IX); (IY).
                     }
                     Error("Closing bracket missing after (IX or (IY");
                  }
               } else Error("Illegal character after (IX or (IY");
            }
         }
      // Parse an expression, starting one token back.
         Cmd--, *ValueP = GetExp(Cmd);
         Type = Cmd->Type, Value = Cmd++->Value; // Get the closing bracket.
         if (Type == OpL && Value == ')') return 0x280; // (Addr).
         Error("Closing bracket missing after (adr)");
      }
   }
// Parse an expression, starting one token back.
   Cmd--, *ValueP = GetExp(Cmd);
   return 0x281; // Return an address.
}

// Test for an opcode.
static void DoOpcode(CommandP &Cmd) {
   Log(2, "DoOpcode( %X )\n", Cmd->Value);
   uint8_t *RamP = RAM + CurPC;
   int32_t Value1 = 0; PatchListP Patch1; int16_t Op1 = 0;
   int32_t Value2 = 0; PatchListP Patch2; int16_t Op2 = 0;
   CheckPC(CurPC); // Detect min, max and overflow (wrap around).
   uint32_t Op0 = Cmd++->Value; // Opcode.
   uint8_t Op0a = Op0 >> 24, Op0b = Op0 >> 16, Op0c = Op0&0xff;
   if (Cmd->Type != 0) {
   // Get the first operand and a patch pointer for it.
      Op1 = GetOperand(Cmd, &Value1), Patch1 = LastPatch;
      if (Cmd->Type == OpL && Cmd->Value == ',') { // A potential second operand.
         Cmd++;
      // Get the second operand and a patch pointer for it.
         Op2 = GetOperand(Cmd, &Value2), Patch2 = LastPatch;
      }
   }
// Helpful for debugging and enhancement.
   Log(3, "Op0: 0x%08x, Op1: 0x%03x, Value1: 0x%08x, Op2: 0x%03x, Value2: 0x%08x\n", Op0, Op1, Value1, Op2, Value2);
   switch (Op0c) { // Opcode class.
   // in/out.
      case 0x00:
         if (Op0a&1) { // out?
         // Swap operands.
            int32_t Op = Op1; Op1 = Op2, Op2 = Op;
            int32_t Value = Value1; Value1 = Value2, Value2 = Value;
            PatchListP Patch = Patch1; Patch1 = Patch2, Patch2 = Patch;
         }
         if ((Op1&0xff0) == 0x300 && Op2 == 0x501) { // in Rd,(C) or out (C),Rs.
            if (Op1 == 0x306) Error("IN (HL),(C) or OUT (C),(HL): invalid combinations");
            else *RamP++ = 0355, *RamP++ = Op0a | ((Op1&7) << 3);
         } else if (Op1 == 0x307 && Op2 == 0x280) { // in A,(Pb) or out (Pb),A.
            *RamP++ = Op0b;
         // Undefined expression: add a single byte and end processing.
            if (Patch2 != nullptr) Patch2->Type = 0, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
            *RamP++ = Value2;
         }
      // Undocumented: in (C).
         else if (!(Op0a&1) && Op1 == 0x501 && Op2 == 0) *RamP++ = 0355, *RamP++ = 0160;
      // Undocumented: out (C),0.
         else if ((Op0a&1) && Op1 == 0x281 && Op2 == 0x501 && Value1 == 0) *RamP++ = 0355, *RamP++ = 0161;
         else Error("operands not allowed for IN/OUT");
      break;
   // One byte opcode without parameter.
      case 0x01:
      // Operands provided: error.
         if (Op1 != 0 || Op2 != 0) Error("operands not allowed"); else *RamP++ = Op0a;
      break;
   // Two byte opcode without parameter.
      case 0x02:
      // Operands provided: error.
         if (Op1 != 0 || Op2 != 0) Error("operands not allowed"); else *RamP++ = Op0a, *RamP++ = Op0b;
      break;
   // rrd (HL) or rld (HL).
      case 0x03:
         if (Op1 != 0x306 && Op1 != 0 || Op2 != 0) Error("Illegal Operand"); else *RamP++ = Op0a, *RamP++ = Op0b;
      break;
   // bit/res/set: First parameter = bit number, second parameter = Aw.
      case 0x04:
         if (Op1 != 0x281 || Value1 < 0 || Value1 > 7) Error("1st operand has to be between 0 and 7");
      // A,B,C,D,E,H,L,(HL).
         else if ((Op2&0xff0) == 0x300) *RamP++ = Op0a, *RamP++ = Op0b | (Value1 << 3) | Op2&7;
      // (IX+Ds); (IY+Ds).
         else if ((Op2&0xff0) == 0x630) {
            *RamP++ = Op2&1? 0375: 0335, *RamP++ = Op0a;
         // Undefined expression: add a single byte and end processing.
            if (Patch2 != nullptr) Patch2->Type = 0, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
            *RamP++ = Value2, *RamP++ = Op0b | (Value1 << 3) | 6;
         } else Error("2nd operand wrong");
      break;
   // im (one parameter: 0,1,2).
      case 0x05:
         if (Op1 != 0x281 || Op2) Error("operand wrong");
         else if (Value1 < 0 || Value1 > 2) Error("Operand value has to be 0, 1 or 2");
         else {
            if (Value1 > 0) Value1++;
            *RamP++ = Op0a, *RamP++ = Op0b | ((Value1&7) << 3);
         }
      break;
   // add,adc,sub,sbc,and,xor,or,cp.
      case 0x06: switch (Op1) {
      // HL
         case 0x312:
         // BC,DE,HL,SP
            if (Op2 >= 0x310 && Op2 <= 0x313) switch (Op0a) {
            // add
               case 0200: *RamP++ = 0011 | ((Op2&3) << 4); break;
            // adc
               case 0210: *RamP++ = 0355, *RamP++ = 0112 | ((Op2&3) << 4); break;
            // sbc
               case 0230: *RamP++ = 0355, *RamP++ = 0102 | ((Op2&3) << 4); break;
               default: Error("Opcode with this <ea> not allowed");
            } else Error("Expecting a double-register");
         break;
      // IX; IY
         case 0x330: case 0x331:
         // Only add IX/IY,RR.
            if (Op0a != 0200) Error("Only ADD IX,[BC,DE,IX,SP] or ADD IY,[BC,DE,IY,SP] are possible");
            switch (Op2) {
            // BC
               case 0x310: *RamP++ = Op1&1? 0375: 0335, *RamP++ = 0011; break;
            // DE
               case 0x311: *RamP++ = Op1&1? 0375: 0335, *RamP++ = 0031; break;
            // IX; IY
               case 0x330: case 0x331:
                  if (Op1 == Op2) *RamP++ = Op1&1? 0375: 0335, *RamP++ = 0051;
                  else Error("Only ADD IX,IY or ADD IY,IY are possible");
               break;
            // SP
               case 0x313: *RamP++ = Op1&1? 0375: 0335, *RamP++ = 0071; break;
               default: Error("Opcode with this <ea> not allowed");
            }
         break;
         default:
         // Accumulator: shift the second operand to the beginning.
            if (Op1 == 0x307 && Op2 != 0) Op1 = Op2, Value1 = Value2, Patch1 = Patch2, Patch2 = nullptr;
            switch (Op1&0xff0) {
            // X,HX.
               case 0x350: *RamP++ = 0335, *RamP++ = Op0a | Op1&7; break;
            // Y,HY.
               case 0x360: *RamP++ = 0375, *RamP++ = Op0a | Op1&7; break;
            // A,B,C,D,E,H,L,(HL).
               case 0x300: *RamP++ = Op0a | Op1&7; break;
            // (IX+Ds); (IY+Ds).
               case 0x630:
                  *RamP++ = Op1&1? 0375: 0335, *RamP++ = Op0a | 6;
               // Expression undefined: add a single byte and end processing.
                  if (Patch1 != nullptr) Patch1->Type = 0, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                  *RamP++ = Value1;
               break;
            // n
               case 0x280:
                  if (Op1 == 0x281) {
                     *RamP++ = Op0b;
                  // Expression undefined: add a single byte and end processing.
                     if (Patch1 != nullptr) Patch1->Type = 0, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                     *RamP++ = Value1;
                     break;
                  }
               default: Error("2nd operand wrong");
            }
      // break; // (@)?
      }
      break;
   // inc, dec, like 0x06 without absolute address.
      case 0x07:
         if (Op2 != 0) Error("2nd operand not allowed");
      // A,B,C,D,E,H,L,(HL).
         if ((Op1&0xff0) == 0x300) *RamP++ = Op0a | ((Op1&7) << 3);
      // (IX+Ds); (IY+Ds).
         else if ((Op1&0xff0) == 0x630) {
            *RamP++ = Op1&1? 0375: 0335, *RamP++ = Op0a | 060;
         // Expression undefined: add a single byte and end processing.
            if (Patch1 != nullptr) Patch1->Type = 0, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
            *RamP++ = Value1;
         } else {
            bool DecFlag = Op0a&1; // true: dec, false: inc.
            switch (Op1) {
            // HX
               case 0x354: *RamP++ = 0335, *RamP++ = DecFlag? 0045: 0044; break;
            // X
               case 0x355: *RamP++ = 0335, *RamP++ = DecFlag? 0055: 0054; break;
            // HY
               case 0x364: *RamP++ = 0375, *RamP++ = DecFlag? 0045: 0044; break;
            // Y
               case 0x365: *RamP++ = 0375, *RamP++ = DecFlag? 0055: 0054; break;
            // BC
               case 0x310: *RamP++ = DecFlag? 0013: 0003; break;
            // DE
               case 0x311: *RamP++ = DecFlag? 0033: 0023; break;
            // HL
               case 0x312: *RamP++ = DecFlag? 0053: 0043; break;
            // HL
               case 0x313: *RamP++ = DecFlag? 0073: 0063; break;
            // IX
               case 0x330: *RamP++ = 0335, *RamP++ = DecFlag? 0053: 0043; break;
            // IY
               case 0x331: *RamP++ = 0375, *RamP++ = DecFlag? 0053: 0043; break;
               default: Error("Addressing mode not allowed");
            }
         }
      break;
   // jp, call, jr (Warning! Different Aw!)
      case 0x08:
         if (Op1 == 0x301) Op1 = 0x403; // Convert register 'C' into condition 'C'.
         switch (Op0a) {
         // jp
            case 0302:
               if (Op1 >= 0x400 && Op1 <= 0x4ff && Op2 == 0x281) { // Cc,Addr
                  *RamP++ = Op0a | ((Op1&7) << 3);
               // Expression undefined: add two bytes and end processing.
                  if (Patch2 != nullptr) Patch2->Type = 1, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                  *RamP++ = Value2, *RamP++ = Value2 >> 8;
               }
            // jp (HL)
               else if (Op1 == 0x306 && Op2 == 0) *RamP++ = 0351;
            // jp (IX)
               else if (Op1 == 0x530 && Op2 == 0) *RamP++ = 0335, *RamP++ = 0351;
            // jp (IY)
               else if (Op1 == 0x531 && Op2 == 0) *RamP++ = 0375, *RamP++ = 0351;
            // jp Js
               else if (Op1 == 0x281 || Op2 == 0) {
                  *RamP++ = Op0b;
               // Expression undefined: add two bytes and end processing.
                  if (Patch1 != nullptr) Patch1->Type = 1, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                  *RamP++ = Value1, *RamP++ = Value1 >> 8;
               } else Error("1st operand wrong");
            break;
         // jr
            case 0040:
               if (Op1 >= 0x400 && Op1 <= 0x403 && Op2 == 0x281) { // Cc,Js
                  *RamP++ = Op0a | ((Op1&7) << 3);
               // Expression undefined: add a PC-relative byte and end processing.
                  if (Patch2 != nullptr) Patch2->Type = 2, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                  *RamP = uint8_t(Value2 - (RamP - RAM) - 1), RamP++;
               } else if (Op1 == 0x281 && Op2 == 0) { // jr Aw
                  *RamP++ = Op0b;
               // Expression undefined: add a PC-relative byte and end processing.
                  if (Patch1 != nullptr) Patch1->Type = 2, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                  *RamP = uint8_t(Value1 - (RamP - RAM) - 1), RamP++;
               } else Error("Condition not allowed");
            break;
         // call
            case 0304:
               if (Op1 >= 0x400 && Op1 <= 0x4ff && Op2 == 0x281) { // Cc,Js
                  *RamP++ = Op0a | ((Op1&7) << 3);
               // Expression undefined: add two bytes and end processing.
                  if (Patch2 != nullptr) Patch2->Type = 1, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                  *RamP++ = Value2, *RamP++ = Value2 >> 8;
               } else if (Op1 == 0x281 && Op2 == 0) { // call Aw
                  *RamP++ = Op0b;
               // Expression undefined: add two bytes and end processing.
                  if (Patch1 != nullptr) Patch1->Type = 1, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                  *RamP++ = Value1, *RamP++ = Value1 >> 8;
               } else Error("1st operand wrong");
            break;
            default: Error("opcode table has a bug");
         }
      break;
      case 0x09:
      // ret error.
         if (Op2 != 0) Error("Too many operands");
      // No condition given: use the normal opcode.
         else if (Op1 == 0) *RamP++ = Op0b;
         else {
            if (Op1 == 0x301) Op1 = 0x403; // Convert register 'C' into condition 'C'.
            if ((Op1&0xf00) != 0x400) Error("Wrong Operand");
            else *RamP++ = Op0a | ((Op1&7) << 3);
         }
      break;
   // rst (00,08,10,18,20,28,30,38).
      case 0x0a:
         if (Op2 != 0) Error("Too many operands");
         else if (Op1 == 0x281) { // n
            int16_t n = -1;
            switch (Value1) {
               case 0: n = 000; break;
               case 1: case 8: n = 010; break;
               case 2: case 10: case 0x10: n = 020; break;
               case 3: case 18: case 0x18: n = 030; break;
               case 4: case 20: case 0x20: n = 040; break;
               case 5: case 28: case 0x28: n = 050; break;
               case 6: case 30: case 0x30: n = 060; break;
               case 7: case 38: case 0x38: n = 070; break;
               default: Error("Only 00,08,10,18,20,28,30,38 are allowed");
            }
            if (n >= 0) *RamP++ = Op0a | n;
         } else Error("Addressing mode not allowed");
      break;
   // djnz
      case 0x0b:
         *RamP++ = Op0a;
      // Expression undefined: add a PC-relative byte and end processing.
         if (Patch1 != nullptr) Patch1->Type = 2, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
         *RamP = uint8_t(Value1 - (RamP - RAM) - 1), RamP++; // Relocate.
      break;
   // ex: (SP),Rw or DE,HL or AF,AF'
      case 0x0c:
      // ex DE,HL
         if (Op1 == 0x311 && Op2 == 0x312) *RamP++ = 0353;
      // ex AF,AF'
         else if (Op1 == 0x323 && Op2 == 0x324) *RamP++ = 0010;
      // ex (SP),HL
         else if (Op1 == 0x513 && Op2 == 0x312) *RamP++ = 0343;
      // ex (SP),IX
         else if (Op1 == 0x513 && Op2 == 0x330) *RamP++ = 0335, *RamP++ = 0343;
      // ex (SP),IY
         else if (Op1 == 0x513 && Op2 == 0x331) *RamP++ = 0375, *RamP++ = 0343;
         else Error("Operand combination not allowed with EX");
      break;
   // ld
      case 0x0d:
         if (Op1 == 0 || Op2 == 0) Error("Operand missing");
         else {
            uint8_t FirstByte = 0;
            switch (Op1) {
            // ld (IX),; ld (IY),
               case 0x530: case 0x531:
                  Op1 = Op1 == 0x530? 0x356: 0x366;
            // HX; X; HY; Y
               case 0x354: case 0x355: case 0x364: case 0x365:
                  *RamP++ = FirstByte = (Op1&0xff0) == 0x350? 0335: 0375;
                  Op1 &= 0xf0f; // Remap H and L.
            // B; C; D; E; H; L; (HL); A
               case 0x300: case 0x301: case 0x302: case 0x303: case 0x304: case 0x305: case 0x306: case 0x307: switch (Op2&0xff0) {
               // ld Aw,(IX), or ld Aw,(IY)
                  case 0x530:
                     Op2 = Op2 == 0x530? 0x356: 0x366;
               // X,HX; Y,HY
                  case 0x350: case 0x360: {
                     bool Flag = (Op2&0xff0) == 0x350;
                     switch (FirstByte) {
                     // IX
                        case 0335:
                           if (!Flag) Error("IX,IY: invalid combination");
                        break;
                     // IY
                        case 0375:
                           if (Flag) Error("IY,IX: invalid combination");
                        break;
                     // Nothing yet.
                        default: *RamP++ = Flag? 0335: 0375; break;
                     }
                     Op2 &= 0xf0f; // Remap H and L.
                  }
               // B,C,D,E,H,L,(HL),A
                  case 0x300: *RamP++ = 0100 | ((Op1&7) << 3) | Op2&7; break;
               // (BC),(DE),(SP)
                  case 0x510:
                     if (Op1 == 0x307) {
                        if (Op2 == 0x510) *RamP++ = 0012;
                        else if (Op2 == 0x511) *RamP++ = 0032;
                        else Error("(SP) not allowed");
                     } else Error("Only LD A,(BC) or LD A,(DE) allowed");
                  break;
               // (IX+Ds), (IY+Ds)
                  case 0x630:
                     if (Op1 != 0x306) { // (HL)
                        *RamP++ = Op2&1? 0375: 0335, *RamP++ = 0106 | ((Op1&7) << 3);
                     // Expression undefined: add a single byte and end processing.
                        if (Patch2 != nullptr) Patch2->Type = 0, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                        *RamP++ = Value2;
                     } else Error("LD (HL),(IX/IY+Ds) not allowed");
                  break;
               // (n), n
                  case 0x280:
                     if (Op2 == 0x281) {
                        *RamP++ = 0006 | ((Op1&7) << 3);
                     // Expression undefined: add a single byte and end processing.
                        if (Patch2 != nullptr) Patch2->Type = 0, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                        *RamP++ = Value2;
                     } else if (Op1 == 0x307) {
                        *RamP++ = 0072;
                     // Expression undefined: add two bytes and end processing.
                        if (Patch2 != nullptr) Patch2->Type = 1, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                        *RamP++ = Value2, *RamP++ = Value2 >> 8;
                     } else Error("Only LD A,(n) allowed");
                  break;
               // I,R
                  case 0x340:
                     if (Op1 == 0x307) *RamP++ = 0355, *RamP++ = Op2 != 0x340? 0127: 0137;
                     else Error("Only LD A,I or LD A,R allowed");
                  break;
                  default: Error("2nd operand wrong");
               }
               break;
            // I,R; I,R
               case 0x340: case 0x341:
               // A
                  if (Op2 == 0x307) *RamP++ = 0355, *RamP++ = Op1 != 0x340? 0107: 0117;
                  else Error("Only LD I,A or LD R,A allowed");
               break;
            // (BC); (DE)
               case 0x510: case 0x511:
               // A
                  if (Op2 == 0x307) *RamP++ = Op1 == 0x510? 0002: 0022;
                  else Error("Only LD (BC),A or LD (DE),A allowed");
               break;
            // (IX+Ds); (IY+Ds)
               case 0x630: case 0x631: switch (Op2) {
               // B; C; D; E; H; L; A
                  case 0x300: case 0x301: case 0x302: case 0x303: case 0x304: case 0x305: case 0x307:
                     *RamP++ = Op1&1? 0375: 0335, *RamP++ = 0160 | Op2&7;
                  // Expression undefined: add a single byte and end processing.
                     if (Patch1 != nullptr) Patch1->Type = 0, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                     *RamP++ = Value1;
                  break;
               // n
                  case 0x281:
                     *RamP++ = Op1&1? 0375: 0335, *RamP++ = 0066;
                  // Expression undefined: add a single byte and end processing.
                     if (Patch1 != nullptr) Patch1->Type = 0, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                     *RamP++ = Value1;
                  // Expression undefined: add a single byte and end processing.
                     if (Patch2 != nullptr) Patch2->Type = 0, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                     *RamP++ = Value2;
                  break;
                  default: Error("2nd operand wrong");
               }
               break;
            // (n)
               case 0x280: switch (Op2) {
               // A
                  case 0x307:
                     *RamP++ = 0062;
                  // Expression undefined: add two bytes and end processing.
                     if (Patch1 != nullptr) Patch1->Type = 1, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                     *RamP++ = Value1, *RamP++ = Value1 >> 8;
                  break;
               // HL
                  case 0x312:
                     *RamP++ = 0042;
                  // Expression undefined: add two bytes and end processing.
                     if (Patch1 != nullptr) Patch1->Type = 1, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                     *RamP++ = Value1, *RamP++ = Value1 >> 8;
                  break;
               // BC; DE; SP
                  case 0x310: case 0x311: case 0x313:
                     *RamP++ = 0355, *RamP++ = 0103 | ((Op2&3) << 4);
                  // Expression undefined: add two bytes and end processing.
                     if (Patch1 != nullptr) Patch1->Type = 1, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                     *RamP++ = Value1, *RamP++ = Value1 >> 8;
                  break;
               // IX; IY
                  case 0x330: case 0x331:
                     *RamP++ = Op2&1? 0375: 0335, *RamP++ = 0042;
                  // Expression undefined: add two bytes and end processing.
                     if (Patch1 != nullptr) Patch1->Type = 1, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                     *RamP++ = Value1, *RamP++ = Value1 >> 8;
                  break;
                  default: Error("2nd operand wrong");
               }
               break;
            // SP
               case 0x313: switch (Op2) {
               // HL
                  case 0x312: *RamP++ = 0371; break;
               // IX
                  case 0x330: *RamP++ = 0335, *RamP++ = 0371; break;
               // IY
                  case 0x331: *RamP++ = 0375, *RamP++ = 0371; break;
               }
            // break; // (@)?
            // BC; DE; HL
               case 0x310: case 0x311: case 0x312:
                  if (Op2 == 0x280 || Op2 == 0x281) { // (n), n
                  // n
                     if (Op2 == 0x281) *RamP++ = 0001 | ((Op1&3) << 4);
                  // (n)
                  // HL
                     else if (Op1 == 0x312) *RamP++ = 0052;
                     else *RamP++ = 0355, *RamP++ = 0113 | ((Op1&3) << 4);
                  // Expression undefined: add two bytes and end processing.
                     if (Patch2 != nullptr) Patch2->Type = 1, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                     *RamP++ = Value2, *RamP++ = Value2 >> 8;
                  } else if (Op2 != 0x312 && Op2 != 0x330 && Op2 != 0x331) Error("2nd operand wrong");
               break;
            // IX; IY
               case 0x330: case 0x331:
                  if (Op2 == 0x280 || Op2 == 0x281) { // (n), n
                     *RamP++ = Op1&1? 0375: 0335, *RamP++ = Op2 == 0x281? 0041: 0052;
                  // Expression undefined: add two bytes and end processing.
                     if (Patch2 != nullptr) Patch2->Type = 1, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                     *RamP++ = Value2, *RamP++ = Value2 >> 8;
                  } else Error("2nd operand wrong");
               break;
               default: Error("Addressing mode not allowed");
            }
         }
      break;
   // push, pop: Rw
      case 0x0e:
         if (Op2 != 0) Error("Too many operands");
         else if ((Op1&0xff0) >= 0x310 && (Op1&0xff0) <= 0x33f) { // Double register?
         // push BC,DE,HL
            if (Op1 >= 0x310 && Op1 <= 0x312) *RamP++ = Op0a | ((Op1 - 0x310) << 4);
         // push AF
            else if (Op1 == 0x323) *RamP++ = Op0a | ((Op1 - 0x320) << 4);
         // push IX,IY
            else if (Op1 == 0x330 || Op1 == 0x331) *RamP++ = Op1&1? 0375: 0335, *RamP++ = Op0b;
         } else Error("only double-registers are allowed");
      break;
   // rr,rl,rrc,rlc,sra,sla,srl
      case 0x0f:
         if (Op2 != 0) Error("Only one operand allowed");
      // B,C,D,E,H,L,(HL),A
         else if ((Op1&0xff0) == 0x300) *RamP++ = 0313, *RamP++ = Op0a | Op1&7;
      // (IX+Ds), (IY+Ds)
         else if (Op1 == 0x630 || Op1 == 0x631) {
            *RamP++ = Op1&1? 0375: 0335, *RamP++ = 0313;
         // Expression undefined: add a single byte and end processing.
            if (Patch1 != nullptr) Patch1->Type = 0, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
            *RamP++ = Value1, *RamP++ = Op0a | 6;
         } else Error("operand not allowed");
      break;
      default:
         Error("unknown opcode type");
         for (; Cmd->Type != 0; Cmd++);
   }
   CurPC = RamP - RAM; // PC -> next opcode
   CheckPC(CurPC - 1); // The last RAM position used>
}

// Test for pseudo-opcodes.
static bool PassOver = false; // Ignore all lines till next "ENDIF" (this could be a stack for nesting support).

static void DoPseudo(CommandP &Cmd) {
   Log(2, "DoPseudo( %d, %X )\n", Cmd->Type, Cmd->Value);
   uint16_t PC = CurPC;
   switch (Cmd++->Value) { // All pseudo-opcodes
      case _db: case _dm:
         Cmd--;
         do {
            Cmd++; // Skip an opcode or comma.
            if (Cmd->Type != StrL) {
               CheckPC(PC), RAM[PC++] = GetExp(Cmd);
            // Expression undefined: add a single byte.
               if (LastPatch != nullptr) LastPatch->Type = 0, LastPatch->Addr = PC - 1;
            } else {
               char *SP = (char *)Cmd++->Value; // Value = pointer to the string.
               CheckPC(PC + strlen(SP) - 1); // Check for overflow.
               while (*SP != '\0') RAM[PC++] = *SP++; // Transfer the string.
            }
         } while (Cmd->Type == OpL && Cmd->Value == ',');
      break;
      case _ds:
      // Advance the PC.
         PC += GetExp(Cmd);
         if (LastPatch != nullptr) Error("symbol not defined");
      break;
      case _fill: {
         uint16_t Fill = 0, Size = GetExp(Cmd); // Get the amount.
         if (LastPatch != nullptr) Error("symbol not defined");
         if (Cmd->Type == OpL && Cmd->Value == ',') { // ", val" part?
            Cmd++, Fill = GetExp(Cmd); // Get the fill value.
            if (LastPatch != nullptr) Error("symbol not defined");
         }
         CheckPC(PC + Size - 1);
         while (Size-- > 0) RAM[PC++] = Fill;
      }
      break;
      case _dw:
         Cmd--;
         do {
            Cmd++;
            uint32_t Value = GetExp(Cmd); // Evaluate the expression.
         // Expression undefined: add two bytes.
            if (LastPatch != nullptr) LastPatch->Type = 1, LastPatch->Addr = PC;
            CheckPC(PC + 1); // Will it overflow?
            RAM[PC++] = Value, RAM[PC++] = Value >> 8;
         } while (Cmd->Type == OpL && Cmd->Value == ',');
      break;
      case _end:
         if (PassOver) Error("IF without ENDIF");
         AtEnd = true;
      break;
   // Set the PC.
      case _org:
         PC = GetExp(Cmd);
         if (LastPatch != nullptr) Error("symbol not defined");
      break;
   // IF condition false: then ignore the next block.
      case _if:
         if (GetExp(Cmd) == 0) PassOver = true;
      break;
   // Never ignore from here on.
      case _endif: PassOver = false; break;
   // Flip the condition.
      case _else: PassOver = !PassOver; break;
      case _print:
         if (Cmd->Type != StrL) Error("PRINT requires a string parameter");
         else puts((char *)Cmd++->Value); // Print a message.
      break;
   }
   CurPC = PC;
}

// Compile a line into machine code.
void CompileLine(void) {
   Log(2, "CompileLine()\n");
   CommandP Cmd = CmdBuf;
   if (Cmd->Type == 0) return; // Empty line => done.
   if (Cmd->Type == SymL && !PassOver) { // The symbol is at the beginning, but not IF?
      SymbolP Sym = (SymbolP)Cmd->Value; // Dereference the symbol.
      if (Sym->Defined) Error("symbol already defined");
      Cmd++; // The next command.
      if (Cmd->Type == OpL && Cmd->Value == ':') Cmd++; // Ignore a ":" after a symbol.
      if (Cmd->Type == OpL && Cmd->Value == _equ) { // EQU?
      // Skip EQU and calculate the expression.
         Cmd++, Sym->Value = GetExp(Cmd);
         if (LastPatch != nullptr) Error("symbol not defined in a formula");
         Sym->Defined = true; // The symbol is now defined.
         if (Cmd->Type != BadL) Error("EQU is followed by illegal data");
      } else Sym->Value = CurPC, Sym->Defined = true; // The symbol is an address defined as the current PC.
      while (Sym->Patch != nullptr) { // Do expressions depend on the symbol?
         PatchListP Patch = Sym->Patch;
         Sym->Patch = Patch->Next; // To the next symbol.
         CommandP Cmd0 = Patch->Cmd;
         int32_t Value = GetExp(Patch->Cmd); // Recalculate the symbol (now with the defined symbol)
         if (LastPatch == nullptr) { // Is the expression now valid? (or is there another open dependency?)
            uint16_t Addr = Patch->Addr;
            switch (Patch->Type) {
            // Add a single byte.
               case 0: List("%04X <- %02X\n", Addr, Value), RAM[Addr] = Value; break;
            // Add two bytes.
               case 1: List("%04X <- %02X %02X\n", Addr, Value&0xff, Value >> 8), RAM[Addr++] = Value, RAM[Addr] = Value >> 8; break;
            // Add a PC-relative byte.
               case 2: Value -= Addr + 1, List("%04X <- %02X\n", Addr, Value), RAM[Addr] = Value; break;
               default: Error("unknown Patch type");
            }
         } else // The expression still can't be calculated: transfer the type and the address.
            LastPatch->Type = Patch->Type, LastPatch->Addr = Patch->Addr;
         free(Cmd0); // Release the formula.
         free(Patch); // Release the Patch term.
      }
   }
   if (PassOver) { // Inside an IF?
      if (Cmd->Type == OpL) switch (Cmd->Value) {
      // ENDIF reached: start compiling.
         case _endif: PassOver = false; break;
      // ELSE reached: toggle IF flag.
         case _else: PassOver = !PassOver; break;
      }
   } else while (Cmd->Type != 0) { // Scan to the end of the line.
      uint16_t Value = Cmd->Value;
   // No Opcode or Pseudo-Opcode: error.
      if (Value < 0x100 || Value > 0x2ff) Error("Illegal token");
   // Pseudo-Opcode or Opcode.
      if (Value >= 0x100 && Value <= 0x1ff) DoPseudo(Cmd); else DoOpcode(Cmd);
   }
}
