// compile a tokenzied line
#include "Cas.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// get operands for an opcode
static int16_t GetOperand(CommandP &Cmd, int32_t *ValueP) {
   LastPatch = nullptr; // (to be safe: reset patch entry)
   *ValueP = 0;
   int16_t Type = Cmd->Type;
   int16_t Value = Cmd++->Value; // get value and type
   Log(2, "GetOperand( %d, %X, %d )\n", Type, Value, *ValueP);
   if (Type == OpL) {
      if ((Value >= 0x300) && (Value <= 0x4ff)) {
         if ((Value == 0x323) && (Cmd->Type == OpL) && (Cmd->Value == '\'')) { // AF'?
            Cmd++; // skip '
            Value = 0x324; // AF' returned
         }
         return Value; // register or condition
      }
      if ((Value >= 0x100) && (Value <= 0x2ff)) // an opcode
         Error("Illegal operand");
      if ((Value == '(') != 0) { // indirect addressing?
         Type = Cmd->Type;
         int16_t SValue = Cmd++->Value; // get value and type
         if (Type == OpL) {
            if (((SValue&0xff0) == 0x310) || (SValue == 0x301)) { // register
               Type = Cmd->Type;
               Value = Cmd++->Value; // get value and type
               if ((Type == OpL) && (Value == ')')) {
                  if (SValue == 0x312) // (HL)?
                     return 0x306; // to combine them easier
                  return SValue + 0x200; // (C),(BC),(DE),(SP)
               }
               Error("Closing bracket missing after (BC,(DE,(HL or (SP");
            }
            if ((SValue&0xff0) == 0x330) { // IX,IY
               if (Cmd->Type == OpL) { // does an operator follow?
                  Value = Cmd->Value;
                  if ((Value == '+') || (Value == '-')) {
                     *ValueP = GetExp(Cmd);
                     Type = Cmd->Type;
                     Value = Cmd++->Value; // get a braket
                     if ((Type == OpL) && (Value == ')'))
                        return SValue + 0x300; // (IX+d) or (IY+d)
                     Error("Closing bracket missing after (IX or (IY");
                  } else {
                     if ((Type == OpL) && (Value == ')')) {
                        Cmd++; // skip the bracket
                        return SValue + 0x200; // (IX) or (IY)
                     }
                     Error("Closing bracket missing after (IX or (IY");
                  }
               } else
                  Error("Illegal character after (IX or (IY");
            }
         }
         Cmd--; // Ptr auf das vorherige Zeichen zurück
         *ValueP = GetExp(Cmd);
         Type = Cmd->Type;
         Value = Cmd++->Value; // get the closing bracket
         if ((Type == OpL) && (Value == ')'))
            return 0x280; // (Adr)
         Error("Closing bracket missing after (adr)");
      }
   } // absolute addressing
   Cmd--; // return to the previous token
   *ValueP = GetExp(Cmd);
   return 0x281; // return an address
}

// test for an opcode
static void DoOpcode(CommandP &Cmd) {
   Log(2, "DoOpcode( %X )\n", Cmd->Value);
   uint8_t *RamP = RAM + CurPC;
   int32_t Value1 = 0, Value2 = 0;
   PatchListP Patch1, Patch2;
   int16_t Op1 = 0, Op2 = 0;
   CheckPC(CurPC); // detect min, max and overflow (wrap around)
   uint32_t Op0 = Cmd++->Value; // opcode
   uint8_t Op0a = Op0 >> 24, Op0b = Op0 >> 16;
   if (Cmd->Type) {
      Op1 = GetOperand(Cmd, &Value1); // get 1. operand
      Patch1 = LastPatch; // store Patch ptr for 1. operand
      if ((Cmd->Type == OpL) && (Cmd->Value == ',')) { // get a potential 2. operand
         Cmd++;
         Op2 = GetOperand(Cmd, &Value2); // get the 2. operand
         Patch2 = LastPatch; // store Patch ptr for 2. operand
      }
   }
// helpful for debugging and enhancement
   Log(3, "Op0: 0x%08x, Op1: 0x%03x, Value1: 0x%08x, Op2: 0x%03x, Value2: 0x%08x\n", Op0, Op1, Value1, Op2, Value2);
   switch (Op0&0xff) { // opcode
      case 0x00: // IN/OUT
         if (Op0a&1) { // OUT?
            int32_t Op = Op1;
            Op1 = Op2;
            Op2 = Op; // flip operands
            int32_t Value = Value1;
            Value1 = Value2;
            Value2 = Value;
            PatchListP Patch = Patch1;
            Patch1 = Patch2;
            Patch2 = Patch;
         }
         if (((Op1&0xff0) == 0x300) && (Op2 == 0x501)) { // IN ?,(C) or OUT (C),?
            if (Op1 == 0x306)
               Error("IN (HL),(C) or OUT (C),(HL) geht nicht");
            else {
               *RamP++ = 0355;
               *RamP++ = Op0a | ((Op1&7) << 3);
            }
         } else if ((Op1 == 0x307) && (Op2 == 0x280)) { // IN A,(n) or OUT (n),A
            *RamP++ = Op0b;
            if (Patch2) { // undefined expression?
               Patch2->Type = 0; // add a single byte
               Patch2->Addr = RamP - RAM;
               Patch2 = nullptr; // processing done
            }
            *RamP++ = Value2;
         } else if (!(Op0a&1) && (Op1 == 0x501) && (Op2 == 0)) { // undoc: IN (C))
            *RamP++ = 0355;
            *RamP++ = 0160;
         } else if ((Op0a&1) && (Op1 == 0x281) && (Op2 == 0x501) && Value1 == 0) { // undoc: OUT (C),0
            *RamP++ = 0355;
            *RamP++ = 0161;
         } else
            Error("operands not allowed for IN/OUT");
      break;
      case 0x01: // one byte opcode without parameter
         if (Op1 | Op2) // operands provied?
            Error("operands not allowed");
         else
            *RamP++ = Op0a;
      break;
      case 0x02: // two byte opcode without parameter
         if (Op1 | Op2) // operands provied?
            Error("operands not allowed");
         else {
            *RamP++ = Op0a;
            *RamP++ = Op0b;
         }
      break;
      case 0x03:
         if (((Op1 != 0x306) && (Op1)) || (Op2)) // RRD (HL) or RLD (HL)
            Error("Illegal Operand");
         else {
            *RamP++ = Op0a;
            *RamP++ = Op0b;
         }
      break;
      case 0x04: // 1.parameter = bit number, 2.parameter = <ea> (BIT,RES,SET)
         if ((Op1 != 0x281) || (Value1 < 0) || (Value1 > 7))
            Error("1st operand has to be between 0 and 7");
         else {
            if ((Op2&0xff0) == 0x300) { // A,B,C,D,E,H,L,(HL)
               *RamP++ = Op0a;
               *RamP++ = Op0b | (Value1 << 3) | (Op2&7);
            } else if ((Op2&0xff0) == 0x630) { // (IX+d) or (IY+d)
               *RamP++ = (Op2&1)? 0375: 0335;
               *RamP++ = Op0a;
               if (Patch2) { // expression undefined?
                  Patch2->Type = 0; // add a single byte
                  Patch2->Addr = RamP - RAM;
                  Patch2 = nullptr; // processing done
               }
               *RamP++ = Value2;
               *RamP++ = Op0b | (Value1 << 3) | 6;
            } else
               Error("2nd operand wrong");
         }
      break;
      case 0x05: // IM (one parameter: 0,1,2)
         if ((Op1 != 0x281) || (Op2))
            Error("operand wrong");
         else {
            if ((Value1 < 0) || (Value1 > 2))
               Error("Operand value has to be 0, 1 or 2");
            else {
               if (Value1 > 0)
                  Value1++;
               *RamP++ = Op0a;
               *RamP++ = Op0b | ((Value1&7) << 3);
            }
         }
      break;
      case 0x06: // ADD,ADC,SUB,SBC,AND,XOR,OR,CP
         switch (Op1) {
            case 0x312: // HL
               if ((Op2 >= 0x310) && (Op2 <= 0x313)) { // BC,DE,HL,SP
                  switch (Op0a) {
                     case 0200: // ADD
                        *RamP++ = 0011 | ((Op2&3) << 4);
                     break;
                     case 0210: // ADC
                        *RamP++ = 0355;
                        *RamP++ = 0112 | ((Op2&3) << 4);
                     break;
                     case 0230: // SBC
                        *RamP++ = 0355;
                        *RamP++ = 0102 | ((Op2&3) << 4);
                     break;
                     default:
                        Error("Opcode with this <ea> not allowed");
                  }
               } else
                  Error("Expecting a double-register");
            break;
            case 0x330: // IX
            case 0x331: // IY
               if (Op0a != 0200) // only ADD IX/IY,RR
                  Error("Only ADD IX,[BC,DE,IX,SP] or ADD IY,[BC,DE,IY,SP] are possible");
               switch (Op2) {
                  case 0x310: // BC
                     *RamP++ = (Op1&1)? 0375: 0335;
                     *RamP++ = 0011;
                  break;
                  case 0x311: // DE
                     *RamP++ = (Op1&1)? 0375: 0335;
                     *RamP++ = 0031;
                  break;
                  case 0x330: // IX
                  case 0x331: // IY
                     if (Op1 == Op2) {
                        *RamP++ = (Op1&1)? 0375: 0335;
                        *RamP++ = 0051;
                     } else
                        Error("Only ADD IX,IY or ADD IY,IY are possible");
                  break;
                  case 0x313: // SP
                     *RamP++ = (Op1&1)? 0375: 0335;
                     *RamP++ = 0071;
                  break;
                  default:
                     Error("Opcode with this <ea> not allowed");
               }
            break;
            default:
               if ((Op1 == 0x307) && (Op2)) { // accumulator?
                  Op1 = Op2;
                  Value1 = Value2; // Shift 2nd operand to the beginning
                  Patch1 = Patch2;
                  Patch2 = nullptr;
               }
               switch (Op1&0xff0) {
                  case 0x350: // X,HX
                     *RamP++ = 0335;
                     *RamP++ = Op0a | (Op1&7);
                  break;
                  case 0x360: // Y,HY
                     *RamP++ = 0375;
                     *RamP++ = Op0a | (Op1&7);
                  break;
                  case 0x300: // A,B,C,D,E,H,L,(HL)
                     *RamP++ = Op0a | (Op1&7);
                  break;
                  case 0x630: // (IX+d) or (IY+d)
                     *RamP++ = (Op1&1)? 0375: 0335;
                     *RamP++ = Op0a | 6;
                     if (Patch1) { // expression undefined?
                        Patch1->Type = 0; // add a single byte
                        Patch1->Addr = RamP - RAM;
                        Patch1 = nullptr; // processing done
                     }
                     *RamP++ = Value1;
                  break;
                  case 0x280: // n
                     if (Op1 == 0x281) {
                        *RamP++ = Op0b;
                        if (Patch1) { // expression undefined?
                           Patch1->Type = 0; // add a single byte
                           Patch1->Addr = RamP - RAM;
                           Patch1 = nullptr; // processing done
                        }
                        *RamP++ = Value1;
                        break;
                     }
                  default:
                     Error("2nd operand wrong");
               }
         }
      break;
      case 0x07: // INC, DEC, like 0x06 without absolute address
         if (Op2)
            Error("2nd operand not allowed");
         if ((Op1&0xff0) == 0x300) { // A,B,C,D,E,H,L,(HL)
            *RamP++ = Op0a | ((Op1&7) << 3);
         } else if ((Op1&0xff0) == 0x630) { // (IX+d) or (IY+d)
            *RamP++ = (Op1&1)? 0375: 0335;
            *RamP++ = Op0a | (6 << 3);
            if (Patch1) { // expression undefined?
               Patch1->Type = 0; // add a single byte
               Patch1->Addr = RamP - RAM;
               Patch1 = nullptr; // processing done
            }
            *RamP++ = Value1;
         } else {
            bool DecFlag = Op0a&1; // True: DEC, False: INC
            switch (Op1) {
               case 0x354: // HX
                  *RamP++ = 0335;
                  *RamP++ = DecFlag? 0045: 0044;
               break;
               case 0x355: // X
                  *RamP++ = 0335;
                  *RamP++ = DecFlag? 0055: 0054;
               break;
               case 0x364: // HY
                  *RamP++ = 0375;
                  *RamP++ = DecFlag? 0045: 0044;
               break;
               case 0x365: // Y
                  *RamP++ = 0375;
                  *RamP++ = DecFlag? 0055: 0054;
               break;
               case 0x310: // BC
                  *RamP++ = DecFlag? 0013: 0003;
               break;
               case 0x311: // DE
                  *RamP++ = DecFlag? 0033: 0023;
               break;
               case 0x312: // HL
                  *RamP++ = DecFlag? 0053: 0043;
               break;
               case 0x313: // HL
                  *RamP++ = DecFlag? 0073: 0063;
               break;
               case 0x330: // IX
                  *RamP++ = 0335;
                  *RamP++ = DecFlag? 0053: 0043;
               break;
               case 0x331: // IY
                  *RamP++ = 0375;
                  *RamP++ = DecFlag? 0053: 0043;
               break;
               default:
                  Error("Addressing mode not allowed");
            }
         }
      break;
      case 0x08: // JP, CALL, JR (Warning! Different <ea>!)
         if (Op1 == 0x301)
            Op1 = 0x403; // convert register 'C' into condition 'C'
         switch (Op0a) {
            case 0302: // JP
               if ((Op1 >= 0x400) && (Op1 <= 0x4ff) && (Op2 == 0x281)) { // cond,Adr
                  *RamP++ = Op0a | ((Op1&7) << 3);
                  if (Patch2) { // expression undefined?
                     Patch2->Type = 1; // add two bytes
                     Patch2->Addr = RamP - RAM;
                     Patch2 = nullptr; // processing done
                  }
                  *RamP++ = Value2;
                  *RamP++ = Value2 >> 8;
               } else if ((Op1 == 0x306) && !Op2) { // JP (HL)
                  *RamP++ = 0351;
               } else if ((Op1 == 0x530) && !Op2) { // JP (IX)
                  *RamP++ = 0335;
                  *RamP++ = 0351;
               } else if ((Op1 == 0x531) && !Op2) { // JP (IY)
                  *RamP++ = 0375;
                  *RamP++ = 0351;
               } else if ((Op1 == 0x281) | !Op2) { // JP Adr
                  *RamP++ = Op0b;
                  if (Patch1) { // expression undefined?
                     Patch1->Type = 1; // add two bytes
                     Patch1->Addr = RamP - RAM;
                     Patch1 = nullptr; // processing done
                  }
                  *RamP++ = Value1;
                  *RamP++ = Value1 >> 8;
               } else
                  Error("1st operand wrong");
            break;
            case 0040: // JR
               if ((Op1 >= 0x400) && (Op1 <= 0x403) && (Op2 == 0x281)) { // Cond,Adr
                  *RamP++ = Op0a | ((Op1&7) << 3);
                  if (Patch2) { // expression undefined?
                     Patch2->Type = 2; // ein PC-rel-Byte einsetzen
                     Patch2->Addr = RamP - RAM;
                     Patch2 = nullptr; // processing done
                  }
                  uint8_t B = Value2 - (RamP - RAM) - 1;
                  *RamP++ = B;
               } else if ((Op1 == 0x281) && !Op2) { // JR Adr
                  *RamP++ = Op0b;
                  if (Patch1) { // expression undefined?
                     Patch1->Type = 2; // ein PC-rel-Byte einsetzen
                     Patch1->Addr = RamP - RAM;
                     Patch1 = nullptr; // processing done
                  }
                  uint8_t B = Value1 - (RamP - RAM) - 1;
                  *RamP++ = B;
               } else
                  Error("Condition not allowed");
            break;
            case 0304: // CALL
               if ((Op1 >= 0x400) && (Op1 <= 0x4ff) && (Op2 == 0x281)) { // Cond,Adr
                  *RamP++ = Op0a | ((Op1&7) << 3);
                  if (Patch2) { // expression undefined?
                     Patch2->Type = 1; // add two bytes
                     Patch2->Addr = RamP - RAM;
                     Patch2 = nullptr; // processing done
                  }
                  *RamP++ = Value2;
                  *RamP++ = Value2 >> 8;
               } else if ((Op1 == 0x281) && !Op2) { // CALL Adr
                  *RamP++ = Op0b;
                  if (Patch1) { // expression undefined?
                     Patch1->Type = 1; // add two bytes
                     Patch1->Addr = RamP - RAM;
                     Patch1 = nullptr; // processing done
                  }
                  *RamP++ = Value1;
                  *RamP++ = Value1 >> 8;
               } else
                  Error("1st operand wrong");
            break;
            default:
               Error("opcode table has a bug");
         }
      break;
      case 0x09:
         if (Op2) // RET-Befehl
            Error("Too many operands");
         else if (!Op1) // keine Condition angegeben?
            *RamP++ = Op0b; // normalen Opcode nehmen
         else {
            if (Op1 == 0x301)
               Op1 = 0x403; // Register C in Condition C wandeln
            if ((Op1&0xf00) != 0x400)
               Error("Wrong Operand");
            else
               *RamP++ = Op0a | ((Op1&7) << 3);
         }
      break;
      case 0x0a: // RST (00,08,10,18,20,28,30,38)
         if (Op2)
            Error("Too many operands");
         else if (Op1 == 0x281) { // n
            int16_t n = -1;
            switch (Value1) {
               case 0:
                  n = 000;
               break;
               case 1:
               case 8:
                  n = 010;
               break;
               case 2:
               case 10:
               case 0x10:
                  n = 020;
               break;
               case 3:
               case 18:
               case 0x18:
                  n = 030;
               break;
               case 4:
               case 20:
               case 0x20:
                  n = 040;
               break;
               case 5:
               case 28:
               case 0x28:
                  n = 050;
               break;
               case 6:
               case 30:
               case 0x30:
                  n = 060;
               break;
               case 7:
               case 38:
               case 0x38:
                  n = 070;
               break;
               default:
                  Error("Only 00,08,10,18,20,28,30,38 are allowed");
            }
            if (n >= 0)
               *RamP++ = Op0a | n;
         } else
            Error("Addressing mode not allowed");
      break;
      case 0x0b: { // DJNZ
         *RamP++ = Op0a;
         if (Patch1) { // expression undefined?
            Patch1->Type = 2; // ein PC-rel-Byte einsetzen
            Patch1->Addr = RamP - RAM;
            Patch1 = nullptr; // processing done
         }
         uint8_t B = Value1 - (RamP - RAM) - 1;
         *RamP++ = B; // relocate
      }
      break;
      case 0x0c: // EX: (SP),dreg or DE,HL or AF,AF'
         if ((Op1 == 0x311) && (Op2 == 0x312)) // EX DE,HL
            *RamP++ = 0353;
         else if ((Op1 == 0x323) && (Op2 == 0x324)) { // EX AF,AF'
            *RamP++ = 0010;
         } else if ((Op1 == 0x513) && (Op2 == 0x312)) // EX (SP),HL
            *RamP++ = 0343;
         else if ((Op1 == 0x513) && (Op2 == 0x330)) { // EX (SP),IX
            *RamP++ = 0335;
            *RamP++ = 0343;
         } else if ((Op1 == 0x513) && (Op2 == 0x331)) { // EX (SP),IY
            *RamP++ = 0375;
            *RamP++ = 0343;
         } else
            Error("Operand combination not allowed with EX");
      break;
      case 0x0d: // LD
         if (!(Op1&Op2))
            Error("Operand missing");
         else {
            uint8_t FirstByte = 0;
            switch (Op1) {
               case 0x530: // LD (IX),
               case 0x531: // LD (IY),
                  Op1 = (Op1 == 0x530)? 0x356: 0x366;
               case 0x354: // HX
               case 0x355: // X
               case 0x364: // HY
               case 0x365: // Y
                  FirstByte = ((Op1&0xff0) == 0x350)? 0335: 0375;
                  *RamP++ = FirstByte;
                  Op1 &= 0xf0f; // remap H and L
               case 0x300: // B
               case 0x301: // C
               case 0x302: // D
               case 0x303: // E
               case 0x304: // H
               case 0x305: // L
               case 0x306: // (HL)
               case 0x307: // A
                  switch (Op2&0xff0) {
                     case 0x530: // LD <ea>,(IX), or LD <ea>,(IY)
                        Op2 = (Op2 == 0x530)? 0x356: 0x366;
                     case 0x350: // X,HX
                     case 0x360: // Y,HY
                     {
                        bool Flag = ((Op2&0xff0) == 0x350);
                        switch (FirstByte) {
                           case 0335: // IX
                              if (!Flag)
                                 Error("IX,IY geht nicht");
                           break;
                           case 0375: // IY
                              if (Flag)
                                 Error("IY,IX geht nicht");
                           break;
                           default: // noch nix
                              *RamP++ = (Flag)? 0335: 0375;
                           break;
                        }
                     }
                        Op2 &= 0xf0f; // remap H and L
                     case 0x300: // B,C,D,E,H,L,(HL),A
                        *RamP++ = 0100 | ((Op1&7) << 3) | (Op2&7);
                     break;
                     case 0x510: // (BC),(DE),(SP)
                        if (Op1 == 0x307) {
                           if (Op2 == 0x510)
                              *RamP++ = 0012;
                           else if (Op2 == 0x511)
                              *RamP++ = 0032;
                           else
                              Error("(SP) not allowed");
                        } else
                           Error("Only LD A,(BC) or LD A,(DE) allowed");
                     break;
                     case 0x630: // (IX+d), (IY+d)
                        if (Op1 != 0x306) { // (HL)
                           *RamP++ = (Op2&1)? 0375: 0335;
                           *RamP++ = 0106 | ((Op1&7) << 3);
                           if (Patch2) { // expression undefined?
                              Patch2->Type = 0; // add a single byte
                              Patch2->Addr = RamP - RAM;
                              Patch2 = nullptr; // processing done
                           }
                           *RamP++ = Value2;
                        } else
                           Error("LD (HL),(IX/IY+d) not allowed");
                     break;
                     case 0x280: // (n), n
                        if (Op2 == 0x281) {
                           *RamP++ = 0006 | ((Op1&7) << 3);
                           if (Patch2) { // expression undefined?
                              Patch2->Type = 0; // add a single byte
                              Patch2->Addr = RamP - RAM;
                              Patch2 = nullptr; // processing done
                           }
                           *RamP++ = Value2;
                        } else {
                           if (Op1 == 0x307) {
                              *RamP++ = 0072;
                              if (Patch2) { // expression undefined?
                                 Patch2->Type = 1; // add two bytes
                                 Patch2->Addr = RamP - RAM;
                                 Patch2 = nullptr; // processing done
                              }
                              *RamP++ = Value2;
                              *RamP++ = Value2 >> 8;
                           } else
                              Error("Only LD A,(n) allowed");
                        }
                     break;
                     case 0x340: // I,R
                        if (Op1 == 0x307) {
                           *RamP++ = 0355;
                           *RamP++ = (Op2 != 0x340)? 0127: 0137;
                        } else
                           Error("Only LD A,I or LD A,R allowed");
                     break;
                     default:
                        Error("2nd operand wrong");
                  }
               break;
               case 0x340: // I,R
               case 0x341: // I,R
                  if (Op2 == 0x307) { // A
                     *RamP++ = 0355;
                     *RamP++ = (Op1 != 0x340)? 0107: 0117;
                  } else
                     Error("Only LD I,A or LD R,A allowed");
               break;
               case 0x510: // (BC)
               case 0x511: // (DE)
                  if (Op2 == 0x307) { // A
                     *RamP++ = (Op1 == 0x510)? 0002: 0022;
                  } else
                     Error("Only LD (BC),A or LD (DE),A allowed");
               break;
               case 0x630: // (IX+d)
               case 0x631: // (IY+d)
                  switch (Op2) {
                     case 0x300: // B
                     case 0x301: // C
                     case 0x302: // D
                     case 0x303: // E
                     case 0x304: // H
                     case 0x305: // L
                     case 0x307: // A
                        *RamP++ = (Op1&1)? 0375: 0335;
                        *RamP++ = 0160 | (Op2&7);
                        if (Patch1) { // expression undefined?
                           Patch1->Type = 0; // add a single byte
                           Patch1->Addr = RamP - RAM;
                           Patch1 = nullptr; // processing done
                        }
                        *RamP++ = Value1;
                     break;
                     case 0x281: // n
                        *RamP++ = (Op1&1)? 0375: 0335;
                        *RamP++ = 0066;
                        if (Patch1) { // expression undefined?
                           Patch1->Type = 0; // add a single byte
                           Patch1->Addr = RamP - RAM;
                           Patch1 = nullptr; // processing done
                        }
                        *RamP++ = Value1;
                        if (Patch2) { // expression undefined?
                           Patch2->Type = 0; // add a single byte
                           Patch2->Addr = RamP - RAM;
                           Patch2 = nullptr; // processing done
                        }
                        *RamP++ = Value2;
                     break;
                     default:
                        Error("2nd operand wrong");
                  }
               break;
               case 0x280: // (n)
                  switch (Op2) {
                     case 0x307: // A
                        *RamP++ = 0062;
                        if (Patch1) { // expression undefined?
                           Patch1->Type = 1; // add two bytes
                           Patch1->Addr = RamP - RAM;
                           Patch1 = nullptr; // processing done
                        }
                        *RamP++ = Value1;
                        *RamP++ = Value1 >> 8;
                     break;
                     case 0x312: // HL
                        *RamP++ = 0042;
                        if (Patch1) { // expression undefined?
                           Patch1->Type = 1; // add two bytes
                           Patch1->Addr = RamP - RAM;
                           Patch1 = nullptr; // processing done
                        }
                        *RamP++ = Value1;
                        *RamP++ = Value1 >> 8;
                     break;
                     case 0x310: // BC
                     case 0x311: // DE
                     case 0x313: // SP
                        *RamP++ = 0355;
                        *RamP++ = 0103 | ((Op2&3) << 4);
                        if (Patch1) { // expression undefined?
                           Patch1->Type = 1; // add two bytes
                           Patch1->Addr = RamP - RAM;
                           Patch1 = nullptr; // processing done
                        }
                        *RamP++ = Value1;
                        *RamP++ = Value1 >> 8;
                     break;
                     case 0x330: // IX
                     case 0x331: // IY
                        *RamP++ = (Op2&1)? 0375: 0335;
                        *RamP++ = 0042;
                        if (Patch1) { // expression undefined?
                           Patch1->Type = 1; // add two bytes
                           Patch1->Addr = RamP - RAM;
                           Patch1 = nullptr; // processing done
                        }
                        *RamP++ = Value1;
                        *RamP++ = Value1 >> 8;
                     break;
                     default:
                        Error("2nd operand wrong");
                  }
               break;
               case 0x313: // SP
                  switch (Op2) {
                     case 0x312: // HL
                        *RamP++ = 0371;
                     break;
                     case 0x330: // IX
                        *RamP++ = 0335;
                        *RamP++ = 0371;
                     break;
                     case 0x331: // IY
                        *RamP++ = 0375;
                        *RamP++ = 0371;
                     break;
                  }
               case 0x310: // BC
               case 0x311: // DE
               case 0x312: // HL
                  if ((Op2 == 0x280) || (Op2 == 0x281)) { // (n), n
                     if (Op2 == 0x281) { // n
                        *RamP++ = 0001 | ((Op1&3) << 4);
                     } else { // (n)
                        if (Op1 == 0x312) // HL
                           *RamP++ = 0052;
                        else {
                           *RamP++ = 0355;
                           *RamP++ = 0113 | ((Op1&3) << 4);
                        }
                     }
                     if (Patch2) { // expression undefined?
                        Patch2->Type = 1; // add two bytes
                        Patch2->Addr = RamP - RAM;
                        Patch2 = nullptr; // processing done
                     }
                     *RamP++ = Value2;
                     *RamP++ = Value2 >> 8;
                  } else if (Op2 != 0x312 && Op2 != 0x330 && Op2 != 0x331)
                     Error("2nd operand wrong");
               break;
               case 0x330: // IX
               case 0x331: // IY
                  if ((Op2 == 0x280) || (Op2 == 0x281)) { // (n), n
                     *RamP++ = (Op1&1)? 0375: 0335;
                     *RamP++ = (Op2 == 0x281)? 0041: 0052;
                     if (Patch2) { // expression undefined?
                        Patch2->Type = 1; // add two bytes
                        Patch2->Addr = RamP - RAM;
                        Patch2 = nullptr; // processing done
                     }
                     *RamP++ = Value2;
                     *RamP++ = Value2 >> 8;
                  } else
                     Error("2nd operand wrong");
               break;
               default:
                  Error("Addressing mode not allowed");
            }
         }
      break;
      case 0x0e: // PUSH, POP: dreg
         if (Op2)
            Error("Too many operands");
         else if (((Op1&0xff0) >= 0x310) && ((Op1&0xff0) <= 0x33f)) { // double register?
            if ((Op1 >= 0x310) && (Op1 <= 0x312))
               *RamP++ = Op0a | ((Op1 - 0x310) << 4); // PUSH BC,DE,HL
            else if (Op1 == 0x323)
               *RamP++ = Op0a | ((Op1 - 0x320) << 4); // PUSH AF
            else if ((Op1 == 0x330) | (Op1 == 0x331)) { // PUSH IX,IY
               *RamP++ = (Op1&1)? 0375: 0335;
               *RamP++ = Op0b;
            }
         } else
            Error("only double-registers are allowed");
      break;
      case 0x0f: // RR,RL,RRC,RLC,SRA,SLA,SRL
         if (Op2)
            Error("Only one operand allowed");
         else if ((Op1&0xff0) == 0x300) { // B,C,D,E,H,L,(HL),A
            *RamP++ = 0313;
            *RamP++ = Op0a | (Op1&7);
         } else if ((Op1 == 0x630) || (Op1 == 0x631)) { // (IX+d), (IY+d)
            *RamP++ = (Op1&1)? 0375: 0335;
            *RamP++ = 0313;
            if (Patch1) { // expression undefined?
               Patch1->Type = 0; // add a single byte
               Patch1->Addr = RamP - RAM;
               Patch1 = nullptr; // processing done
            }
            *RamP++ = Value1;
            *RamP++ = Op0a | 6;
         } else
            Error("operand not allowed");
      break;
      default:
         Error("unknown opcode type");
         while (Cmd->Type)
            Cmd++;
   }
   CurPC = RamP - RAM; // PC -> next opcode
   CheckPC(CurPC - 1); // last RAM position used
}

// test for pseudo-opcodes
static bool PassOver = false; // ignore all lines till next "ENDIF" (this could be a stack for nesting support)

static void DoPseudo(CommandP &Cmd) {
   Log(2, "DoPseudo( %d, %X )\n", Cmd->Type, Cmd->Value);
   uint16_t PC = CurPC;
   switch (Cmd++->Value) { // all pseudo opcodes
      case _db:
      case _dm:
         Cmd--;
         do {
            Cmd++; // skip opcode or comma
            if (Cmd->Type != StrL) {
               CheckPC(PC);
               RAM[PC++] = GetExp(Cmd);
               if (LastPatch) { // expression undefined?
                  LastPatch->Type = 0; // add a single byte
                  LastPatch->Addr = PC - 1;
               }
            } else {
               char *SP = (char *)Cmd++->Value; // value = ptr to the string
               CheckPC(PC + strlen(SP) - 1); // will it overflow?
               while (*SP)
                  RAM[PC++] = *SP++; // transfer the string
            }
         } while ((Cmd->Type == OpL) && (Cmd->Value == ','));
      break;
      case _ds:
         PC += GetExp(Cmd); // advance the PC
         if (LastPatch)
            Error("symbol not defined");
      break;
      case _dw:
         Cmd--;
         do {
            Cmd++;
            uint32_t Value = GetExp(Cmd); // evaluate the express
            if (LastPatch) { // expression undefined?
               LastPatch->Type = 1; // add two bytes
               LastPatch->Addr = PC;
            }
            CheckPC(PC + 1); // will it overflow?
            RAM[PC++] = Value;
            RAM[PC++] = Value >> 8;
         } while ((Cmd->Type == OpL) && (Cmd->Value == ','));
      break;
      case _end:
         if (PassOver)
            Error("IF without ENDIF");
         Error("Reached the end of the source code -> exit");
      exit(0);
      case _org:
         PC = GetExp(Cmd); // set the PC
         if (LastPatch)
            Error("symbol not defined");
      break;
      case _if:
         if (!GetExp(Cmd)) // IF condition false?
            PassOver = true; // then ignore the next block
      break;
      case _endif:
         PassOver = false; // never ignore from here on
      break;
      case _else:
         PassOver = !PassOver; // flip the condition
      break;
      case _print:
         if (Cmd->Type != StrL)
            Error("PRINT requires a string parameter");
         else
            puts((char *)Cmd++->Value); // print a message
      break;
   }
   CurPC = PC;
}

// Compile a single line into machine code
void CompileLine(void) {
   Log(2, "CompileLine()\n");
   CommandP Cmd = CmdBuf;
   if (!Cmd->Type)
      return; // empty line => done
   if ((Cmd->Type == SymL) && !PassOver) { // symbol at the beginning, but not IF?
      SymbolP Sym = (SymbolP)Cmd->Value; // value = ptr to the symbol
      if (Sym->Defined) {
         Error("symbol already defined");
         return;
      }
      Cmd++; // next command
      if ((Cmd->Type == OpL) && (Cmd->Value == ':'))
         Cmd++; // ignore a ":" after a symbol
      if ((Cmd->Type == OpL) && (Cmd->Value == 0x105)) { // EQU?
         Cmd++; // skip EQU
         Sym->Value = GetExp(Cmd); // calculate the expression
         if (LastPatch)
            Error("symbol not defined in a formula");
         Sym->Defined = true; // symbol now defined
         if (Cmd->Type != BadL) {
            Error("EQU is followed by illegal data");
            return;
         }
      } else {
         Sym->Value = CurPC; // adresse = current PC
         Sym->Defined = true; // symbol is defined
      }
      while (Sym->Patch) { // do expressions depend on the symbol?
         PatchListP Patch = Sym->Patch;
         Sym->Patch = Patch->Next; // to the next symbol
         CommandP Cmd0 = Patch->Cmd;
         int32_t Value = GetExp(Patch->Cmd); // Recalculate the symbol (now with the defined symbol)
         if (!LastPatch) { // Is the expression now valid? (or is there another open dependency?)
            uint16_t Addr = Patch->Addr;
            switch (Patch->Type) {
               case 0: // add a single byte
                  List("%04X <- %02X\n", Addr, Value);
                  RAM[Addr] = Value;
               break;
               case 1: // add two bytes
                  List("%04X <- %02X %02X\n", Addr, Value&0xff, Value >> 8);
                  RAM[Addr++] = Value;
                  RAM[Addr] = Value >> 8;
               break;
               case 2: // PC-rel-byte
                  Value -= (Addr + 1);
                  List("%04X <- %02X\n", Addr, Value);
                  RAM[Addr] = Value;
               break;
               default:
                  Error("unknown Patch type");
            }
         } else { // Expression still can't be calculated
            LastPatch->Type = Patch->Type; // transfer the type
            LastPatch->Addr = Patch->Addr; // transfer the address
         }
         free(Cmd0); // release the formula
         free(Patch); // release the Patch term
      }
   }
   if (PassOver) { // inside an IFs?
      if (Cmd->Type == OpL) {
         switch (Cmd->Value) {
            case 0x108: // ENDIF reached?
               PassOver = false; // start compiling
            break;
            case 0x109: // ELSE reached?
               PassOver = !PassOver; // toggle IF flag
            break;
         }
      }
   } else
      while (Cmd->Type) { // scan to the end of the line
         uint16_t Value = Cmd->Value;
         if ((Value < 0x100) || (Value > 0x2ff)) // no Opcode or Pseudo-Opcode?
            Error("Illegal token"); // => error
         if ((Value >= 0x100) && (Value <= 0x1ff)) // Pseudo-Opcode
            DoPseudo(Cmd);
         else
            DoOpcode(Cmd); // opcode
      }
}
