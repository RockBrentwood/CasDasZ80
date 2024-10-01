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
   if (Type == OpL) {
      switch (LexC(Value)) {
      // PseudoOp; Mnemonic
         case _OpP: case _Op: Error("Illegal operand");
      // Register; Condition
         case _Reg: case _Cc:
         // AF': skip "'" and return AF'.
            if (Value == _AF && Cmd->Type == OpL && Cmd->Value == '\'') Cmd++, Value = _AFx;
         return Value;
      }
      if (Value == '(') { // Indirect addressing?
         int16_t SType = Cmd->Type, SValue = Cmd++->Value; // Get a value and type.
         if (SType == OpL) {
            if (LexT(SValue) == _Rw || SValue == _C) { // Register.
               Type = Cmd->Type, Value = Cmd++->Value; // Get a value and type.
            // (C);(BC);(DE);(HL);(SP); (HL) is handled separately to combine them easier.
               if (Type == OpL && Value == ')') return SValue == _HL? _pHL: _p(SValue);
               else Error("Closing bracket missing after (BC, (DE, (HL or (SP");
            }
            if (LexT(SValue) == _Rx) { // IX,IY
               if (Cmd->Type == OpL) { // Does an operator follow?
                  Value = Cmd->Value;
                  if (Value == '+' || Value == '-') {
                     *ValueP = GetExp(Cmd);
                     Type = Cmd->Type, Value = Cmd++->Value; // Get a bracket.
                     if (Type == OpL && Value == ')') return _x(SValue); // (IX+Ds); (IY+Ds).
                     Error("Closing bracket missing after (IX or (IY");
                  } else {
                     if (Type == OpL && Value == ')') {
                        Cmd++; // Skip the bracket.
                        return _p(SValue); // (IX);(IY).
                     }
                     Error("Closing bracket missing after (IX or (IY");
                  }
               } else Error("Illegal character after (IX or (IY");
            }
         }
      // Parse an expression, starting one token back.
         Cmd--, *ValueP = GetExp(Cmd);
         Type = Cmd->Type, Value = Cmd++->Value; // Get the closing bracket.
         if (Type == OpL && Value == ')') return _Aw; // (Addr).
         Error("Closing bracket missing after (Addr)");
      }
   }
// Parse an expression, starting one token back.
   Cmd--, *ValueP = GetExp(Cmd);
   return _Dw; // Return an address.
}

// Test for an opcode.
static void DoOpcode(CommandP &Cmd) {
   uint8_t *RamP = RAM + CurPC;
   int32_t Value1 = 0; PatchListP Patch1; int16_t Op1 = 0;
   int32_t Value2 = 0; PatchListP Patch2; int16_t Op2 = 0;
   CheckPC(CurPC); // Detect min, max and overflow (wrap around).
   uint32_t Op0 = Cmd++->Value; // Opcode.
   uint8_t Op0a = Op0 >> 24, Op0b = Op0 >> 16;
   uint16_t Op0c = Op0&0xffff;
   if (Cmd->Type != 0) {
   // Get the first operand and a patch pointer for it.
      Op1 = GetOperand(Cmd, &Value1), Patch1 = LastPatch;
      if (Cmd->Type == OpL && Cmd->Value == ',') { // A potential second operand.
         Cmd++;
      // Get the second operand and a patch pointer for it.
         Op2 = GetOperand(Cmd, &Value2), Patch2 = LastPatch;
      }
   }
   switch (Op0c) { // Opcode class.
   // in A,(P); out (P),A.
      case _POp:
         if (Op0a&1) { // out?
         // Swap operands.
            int32_t Op = Op1; Op1 = Op2, Op2 = Op;
            int32_t Value = Value1; Value1 = Value2, Value2 = Value;
            PatchListP Patch = Patch1; Patch1 = Patch2, Patch2 = Patch;
         }
         if (LexT(Op1) == _Rb && Op2 == _p(_C)) { // in Rd,(C) or out (C),Rs.
            if (Op1 == _pHL) Error("IN (HL),(C) or OUT (C),(HL): invalid combinations");
            else *RamP++ = 0355, *RamP++ = Op0a | (LexN(Op1) << 3);
         } else if (Op1 == _A && Op2 == _Aw) { // in A,(Pb) or out (Pb),A.
            *RamP++ = Op0b;
         // Undefined expression: add a single byte and end processing.
            if (Patch2 != nullptr) Patch2->Type = 0, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
            *RamP++ = Value2;
         }
      // Undocumented: in (C).
         else if (!(Op0a&1) && Op1 == _p(_C) && Op2 == 0) *RamP++ = 0355, *RamP++ = 0160;
      // Undocumented: out (C),0.
         else if ((Op0a&1) && Op1 == _Dw && Op2 == _p(_C) && Value1 == 0) *RamP++ = 0355, *RamP++ = 0161;
         else Error("operands not allowed for IN/OUT");
      break;
   // one byte opcode, no parameter.
      case _UnOp:
      // Operands provided: error.
         if (Op1 != 0 || Op2 != 0) Error("operands not allowed"); else *RamP++ = Op0a;
      break;
   // two byte opcode, no parameter.
      case _BinOp:
      // Operands provided: error.
         if (Op1 != 0 || Op2 != 0) Error("operands not allowed"); else *RamP++ = Op0a, *RamP++ = Op0b;
      break;
   // two byte opcode, (HL) required; i.e. rrd (HL); rld (HL).
      case _OpHL:
         if (Op1 != _pHL && Op1 != 0 || Op2 != 0) Error("Illegal Operand"); else *RamP++ = Op0a, *RamP++ = Op0b;
      break;
   // bit n,Rb; res n,Rb; set n,Rb
      case _BitOp:
         if (Op1 != _Dw || Value1 < 0 || Value1 > 7) Error("1st operand has to be between 0 and 7");
      // A;B;C;D;E;H;L;(HL).
         else if (LexT(Op2) == _Rb) *RamP++ = Op0a, *RamP++ = Op0b | (Value1 << 3) | LexN(Op2);
      // (IX+Ds);(IY+Ds).
         else if (LexT(Op2) == _x(_Rx)) {
            *RamP++ = Op2&1? 0375: 0335, *RamP++ = Op0a;
         // Undefined expression: add a single byte and end processing.
            if (Patch2 != nullptr) Patch2->Type = 0, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
            *RamP++ = Value2, *RamP++ = Op0b | (Value1 << 3) | 6;
         } else Error("2nd operand wrong");
      break;
   // im n (n: 0,1,2).
      case _im:
         if (Op1 != _Dw || Op2 != 0) Error("operand wrong");
         else if (Value1 < 0 || Value1 > 2) Error("Operand value has to be 0, 1 or 2");
         else {
            if (Value1 > 0) Value1++;
            *RamP++ = Op0a, *RamP++ = Op0b | ((Value1&7) << 3);
         }
      break;
   // AOp D,S (AOp: add,adc,sub,sbc,and,xor,or,cp).
      case _AOp: switch (Op1) {
         case _HL:
         // BC;DE;HL;SP
            if (LexT(Op2) == _Rw) switch (Op0a) {
            // add
               case 0200: *RamP++ = 0011 | ((Op2&3) << 4); break;
            // adc
               case 0210: *RamP++ = 0355, *RamP++ = 0112 | ((Op2&3) << 4); break;
            // sbc
               case 0230: *RamP++ = 0355, *RamP++ = 0102 | ((Op2&3) << 4); break;
               default: Error("Opcode with this <ea> not allowed");
            } else Error("Expecting a double-register");
         break;
         case _IX: case _IY:
         // Only add Rx,Rw
            if (Op0a != 0200) Error("Only ADD IX,[BC,DE,IX,SP] or ADD IY,[BC,DE,IY,SP] are possible");
            switch (Op2) {
               case _BC: *RamP++ = Op1&1? 0375: 0335, *RamP++ = 0011; break;
               case _DE: *RamP++ = Op1&1? 0375: 0335, *RamP++ = 0031; break;
               case _IX: case _IY:
                  if (Op1 == Op2) *RamP++ = Op1&1? 0375: 0335, *RamP++ = 0051;
                  else Error("Only ADD IX,IY or ADD IY,IY are possible");
               break;
               case _SP: *RamP++ = Op1&1? 0375: 0335, *RamP++ = 0071; break;
               default: Error("Opcode with this <ea> not allowed");
            }
         break;
         default:
         // Accumulator: shift the second operand to the beginning.
            if (Op1 == _A && Op2 != 0) Op1 = Op2, Value1 = Value2, Patch1 = Patch2, Patch2 = nullptr;
            switch (LexT(Op1)) {
            // LX;HX.
               case _Xb: *RamP++ = 0335, *RamP++ = Op0a | LexN(Op1); break;
            // LY;HY.
               case _Yb: *RamP++ = 0375, *RamP++ = Op0a | LexN(Op1); break;
            // A;B;C;D;E;H;L;(HL).
               case _Rb: *RamP++ = Op0a | LexN(Op1); break;
            // (IX+Ds);(IY+Ds).
               case _x(_Rx):
                  *RamP++ = Op1&1? 0375: 0335, *RamP++ = Op0a | 6;
               // Expression undefined: add a single byte and end processing.
                  if (Patch1 != nullptr) Patch1->Type = 0, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                  *RamP++ = Value1;
               break;
            // (Aw);Dw
               case _W:
                  if (Op1 == _Dw) {
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
   // IOp D (IOp: inc,dec), like case 0x06 with absolute address.
      case _IOp:
         if (Op2 != 0) Error("2nd operand not allowed");
      // A;B;C;D;E;H;L;(HL).
         if (LexT(Op1) == _Rb) *RamP++ = Op0a | (LexN(Op1) << 3);
      // (IX+Ds);(IY+Ds).
         else if (LexT(Op1) == _x(_Rx)) {
            *RamP++ = Op1&1? 0375: 0335, *RamP++ = Op0a | 060;
         // Expression undefined: add a single byte and end processing.
            if (Patch1 != nullptr) Patch1->Type = 0, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
            *RamP++ = Value1;
         } else {
            bool DecFlag = Op0a&1; // true: dec, false: inc.
            switch (Op1) {
               case _HX: *RamP++ = 0335, *RamP++ = DecFlag? 0045: 0044; break;
               case _LX: *RamP++ = 0335, *RamP++ = DecFlag? 0055: 0054; break;
               case _HY: *RamP++ = 0375, *RamP++ = DecFlag? 0045: 0044; break;
               case _LY: *RamP++ = 0375, *RamP++ = DecFlag? 0055: 0054; break;
               case _BC: *RamP++ = DecFlag? 0013: 0003; break;
               case _DE: *RamP++ = DecFlag? 0033: 0023; break;
               case _HL: *RamP++ = DecFlag? 0053: 0043; break;
               case _SP: *RamP++ = DecFlag? 0073: 0063; break;
               case _IX: *RamP++ = 0335, *RamP++ = DecFlag? 0053: 0043; break;
               case _IY: *RamP++ = 0375, *RamP++ = DecFlag? 0053: 0043; break;
               default: Error("Addressing mode not allowed");
            }
         }
      break;
   // jp [Cc,]Aw; call [Cc,]Aw; jr [Cc,]Js
      case _RefOp:
         if (Op1 == _C) Op1 = _cC; // Convert register 'C' into condition 'C'.
         switch (Op0a) {
         // jp [Cc,]Aw
            case 0302:
               if (LexC(Op1) == _Cc && Op2 == _Dw) { // jp Cc,Aw
                  *RamP++ = Op0a | (LexN(Op1) << 3);
               // Expression undefined: add two bytes and end processing.
                  if (Patch2 != nullptr) Patch2->Type = 1, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                  *RamP++ = Value2, *RamP++ = Value2 >> 8;
               }
            // jp (HL)
               else if (Op1 == _pHL && Op2 == 0) *RamP++ = 0351;
            // jp (IX)
               else if (Op1 == _p(_IX) && Op2 == 0) *RamP++ = 0335, *RamP++ = 0351;
            // jp (IY)
               else if (Op1 == _p(_IY) && Op2 == 0) *RamP++ = 0375, *RamP++ = 0351;
            // jp Aw
               else if (Op1 == _Dw && Op2 == 0) {
                  *RamP++ = Op0b;
               // Expression undefined: add two bytes and end processing.
                  if (Patch1 != nullptr) Patch1->Type = 1, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                  *RamP++ = Value1, *RamP++ = Value1 >> 8;
               } else Error("1st operand wrong");
            break;
         // jr [Cc,]Js; jr Aw
            case 0040:
               if (IsCc0(Op1) && Op2 == _Dw) { // jr Cc,Js; (Cc: NZ;Z;NC;C)
                  *RamP++ = Op0a | (LexN(Op1) << 3);
               // Expression undefined: add a PC-relative byte and end processing.
                  if (Patch2 != nullptr) Patch2->Type = 2, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                  *RamP = uint8_t(Value2 - (RamP - RAM) - 1), RamP++;
               } else if (Op1 == _Dw && Op2 == 0) { // jr Aw
                  *RamP++ = Op0b;
               // Expression undefined: add a PC-relative byte and end processing.
                  if (Patch1 != nullptr) Patch1->Type = 2, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                  *RamP = uint8_t(Value1 - (RamP - RAM) - 1), RamP++;
               } else Error("Condition not allowed");
            break;
         // call [Cc,]Js; call Aw
            case 0304:
               if (LexC(Op1) == _Cc && Op2 == _Dw) { // call Cc,Js
                  *RamP++ = Op0a | (LexN(Op1) << 3);
               // Expression undefined: add two bytes and end processing.
                  if (Patch2 != nullptr) Patch2->Type = 1, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                  *RamP++ = Value2, *RamP++ = Value2 >> 8;
               } else if (Op1 == _Dw && Op2 == 0) { // call Aw
                  *RamP++ = Op0b;
               // Expression undefined: add two bytes and end processing.
                  if (Patch1 != nullptr) Patch1->Type = 1, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                  *RamP++ = Value1, *RamP++ = Value1 >> 8;
               } else Error("1st operand wrong");
            break;
            default: Error("opcode table has a bug");
         }
      break;
   // ret [Cc]
      case _ret:
      // ret error.
         if (Op2 != 0) Error("Too many operands");
      // No condition given: use the normal opcode.
         else if (Op1 == 0) *RamP++ = Op0b;
         else {
            if (Op1 == _C) Op1 = _cC; // Convert register 'C' into condition 'C'.
            if (LexC(Op1) != _Cc) Error("Wrong Operand");
            else *RamP++ = Op0a | (LexN(Op1) << 3);
         }
      break;
   // rst n (n: 00,08,10,18,20,28,30,38).
      case _rst:
         if (Op2 != 0) Error("Too many operands");
         else if (Op1 == _Dw) { // n
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
   // djnz Js.
      case _djnz:
         *RamP++ = Op0a;
      // Expression undefined: add a PC-relative byte and end processing.
         if (Patch1 != nullptr) Patch1->Type = 2, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
         *RamP = uint8_t(Value1 - (RamP - RAM) - 1), RamP++; // Relocate.
      break;
   // ex (SP),Rw; ex DE,HL; ex AF,AF'.
      case _ex:
      // ex DE,HL
         if (Op1 == _DE && Op2 == _HL) *RamP++ = 0353;
      // ex AF,AF'
         else if (Op1 == _AF && Op2 == _AFx) *RamP++ = 0010;
      // ex (SP),HL
         else if (Op1 == _p(_SP) && Op2 == _HL) *RamP++ = 0343;
      // ex (SP),IX
         else if (Op1 == _p(_SP) && Op2 == _IX) *RamP++ = 0335, *RamP++ = 0343;
      // ex (SP),IY
         else if (Op1 == _p(_SP) && Op2 == _IY) *RamP++ = 0375, *RamP++ = 0343;
         else Error("Operand combination not allowed with EX");
      break;
   // ld D,S.
      case _ld:
         if (Op1 == 0 || Op2 == 0) Error("Operand missing");
         else {
            uint8_t FirstByte = 0;
            switch (Op1) {
            // ld (IX),⋯; ld (IY),⋯
               case _p(_IX): case _p(_IY):
                  Op1 = Op1 == _p(_IX)? _pIX: _pIY;
            // HX; X; HY; Y
               case _HX: case _LX: case _HY: case _LY:
                  *RamP++ = FirstByte = LexT(Op1) == _Xb? 0335: 0375;
                  Op1 &= ~0xf0; // Remap H and L.
            // B; C; D; E; H; L; (HL); A
               case _B: case _C: case _D: case _E: case _H: case _L: case _pHL: case _A: switch (LexT(Op2)) {
               // ld Aw,(IX); ld Aw,(IY)
                  case _p(_Rx):
                     Op2 = Op2 == _p(_IX)? _pIX: _pIY;
               // X,HX; Y,HY
                  case _Xb: case _Yb: {
                     bool IsXb = LexT(Op2) == _Xb;
                     switch (FirstByte) {
                     // IX
                        case 0335:
                           if (!IsXb) Error("IX,IY: invalid combination");
                        break;
                     // IY
                        case 0375:
                           if (IsXb) Error("IY,IX: invalid combination");
                        break;
                     // Nothing yet.
                        default: *RamP++ = IsXb? 0335: 0375; break;
                     }
                     Op2 &= ~0xf0; // Remap H and L.
                  }
               // B;C;D;E;H;L;(HL);A
                  case _Rb: *RamP++ = 0100 | (LexN(Op1) << 3) | LexN(Op2); break;
               // (BC);(DE);(SP)
                  case _p(_Rw):
                     if (Op1 == _A) {
                        if (Op2 == _p(_BC)) *RamP++ = 0012;
                        else if (Op2 == _p(_DE)) *RamP++ = 0032;
                        else Error("(SP) not allowed");
                     } else Error("Only LD A,(BC) or LD A,(DE) allowed");
                  break;
               // (IX+Ds);(IY+Ds)
                  case _x(_Rx):
                     if (Op1 != _pHL) { // (HL)
                        *RamP++ = Op2&1? 0375: 0335, *RamP++ = 0106 | (LexN(Op1) << 3);
                     // Expression undefined: add a single byte and end processing.
                        if (Patch2 != nullptr) Patch2->Type = 0, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                        *RamP++ = Value2;
                     } else Error("LD (HL),(IX/IY+Ds) not allowed");
                  break;
               // (Aw);Dw
                  case _W:
                     if (Op2 == _Dw) {
                        *RamP++ = 0006 | (LexN(Op1) << 3);
                     // Expression undefined: add a single byte and end processing.
                        if (Patch2 != nullptr) Patch2->Type = 0, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                        *RamP++ = Value2;
                     } else if (Op1 == _A) {
                        *RamP++ = 0072;
                     // Expression undefined: add two bytes and end processing.
                        if (Patch2 != nullptr) Patch2->Type = 1, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                        *RamP++ = Value2, *RamP++ = Value2 >> 8;
                     } else Error("Only LD A,(Aw) allowed");
                  break;
               // I;R
                  case _Ri:
                     if (Op1 == _A) *RamP++ = 0355, *RamP++ = Op2 != _R? 0127: 0137;
                     else Error("Only LD A,I or LD A,R allowed");
                  break;
                  default: Error("2nd operand wrong");
               }
               break;
            // I,A;R,A
               case _R: case _I:
                  if (Op2 == _A) *RamP++ = 0355, *RamP++ = Op1 != _R? 0107: 0117;
                  else Error("Only LD I,A or LD R,A allowed");
               break;
            // (BC);(DE)
               case _p(_BC): case _p(_DE):
                  if (Op2 == _A) *RamP++ = Op1 == _p(_BC)? 0002: 0022;
                  else Error("Only LD (BC),A or LD (DE),A allowed");
               break;
            // (IX+Ds);(IY+Ds)
               case _x(_IX): case _x(_IY): switch (Op2) {
               // B;C;D;E;H;L;A
                  case _B: case _C: case _D: case _E: case _H: case _L: case _A:
                     *RamP++ = Op1&1? 0375: 0335, *RamP++ = 0160 | LexN(Op2);
                  // Expression undefined: add a single byte and end processing.
                     if (Patch1 != nullptr) Patch1->Type = 0, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                     *RamP++ = Value1;
                  break;
                  case _Dw:
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
            // (Aw)
               case _Aw: switch (Op2) {
                  case _A:
                     *RamP++ = 0062;
                  // Expression undefined: add two bytes and end processing.
                     if (Patch1 != nullptr) Patch1->Type = 1, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                     *RamP++ = Value1, *RamP++ = Value1 >> 8;
                  break;
                  case _HL:
                     *RamP++ = 0042;
                  // Expression undefined: add two bytes and end processing.
                     if (Patch1 != nullptr) Patch1->Type = 1, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                     *RamP++ = Value1, *RamP++ = Value1 >> 8;
                  break;
                  case _BC: case _DE: case _SP:
                     *RamP++ = 0355, *RamP++ = 0103 | ((Op2&3) << 4);
                  // Expression undefined: add two bytes and end processing.
                     if (Patch1 != nullptr) Patch1->Type = 1, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                     *RamP++ = Value1, *RamP++ = Value1 >> 8;
                  break;
                  case _IX: case _IY:
                     *RamP++ = Op2&1? 0375: 0335, *RamP++ = 0042;
                  // Expression undefined: add two bytes and end processing.
                     if (Patch1 != nullptr) Patch1->Type = 1, Patch1->Addr = RamP - RAM, Patch1 = nullptr;
                     *RamP++ = Value1, *RamP++ = Value1 >> 8;
                  break;
                  default: Error("2nd operand wrong");
               }
               break;
               case _SP: switch (Op2) {
                  case _HL: *RamP++ = 0371; break;
                  case _IX: *RamP++ = 0335, *RamP++ = 0371; break;
                  case _IY: *RamP++ = 0375, *RamP++ = 0371; break;
               }
            // break; // (@)?
               case _BC: case _DE: case _HL:
                  if (Op2 == _Aw || Op2 == _Dw) { // (Aw); Dw
                     if (Op2 == _Dw) *RamP++ = 0001 | ((Op1&3) << 4);
                     else if (Op1 == _HL) *RamP++ = 0052;
                     else *RamP++ = 0355, *RamP++ = 0113 | ((Op1&3) << 4);
                  // Expression undefined: add two bytes and end processing.
                     if (Patch2 != nullptr) Patch2->Type = 1, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                     *RamP++ = Value2, *RamP++ = Value2 >> 8;
                  } else if (Op2 != _HL && Op2 != _IX && Op2 != _IY) Error("2nd operand wrong");
               break;
               case _IX: case _IY:
                  if (Op2 == _Aw || Op2 == _Dw) { // (Aw); Dw
                     *RamP++ = Op1&1? 0375: 0335, *RamP++ = Op2 == _Dw? 0041: 0052;
                  // Expression undefined: add two bytes and end processing.
                     if (Patch2 != nullptr) Patch2->Type = 1, Patch2->Addr = RamP - RAM, Patch2 = nullptr;
                     *RamP++ = Value2, *RamP++ = Value2 >> 8;
                  } else Error("2nd operand wrong");
               break;
               default: Error("Addressing mode not allowed");
            }
         }
      break;
   // push Rw; pop Rw.
      case _StOp:
         if (Op2 != 0) Error("Too many operands");
         else if (IsRbb(Op1)) { // Double register?
         // push Rw1 (Rw1: BC,DE,HL,AF)
            if (LexT(Op1) == _Rw && Op1 != _SP || Op1 == _AF) *RamP++ = Op0a | (LexN(Op1) << 4);
         // push Rx (Rx: IX,IY)
            else if (LexT(Op1) == _Rx) *RamP++ = Op1 == _IY? 0375: 0335, *RamP++ = Op0b;
         } else Error("only double-registers are allowed");
      break;
   // ShOp D (ShOp: rr,rl,rrc,rlc,sra,sla,srl).
      case _ShOp:
         if (Op2 != 0) Error("Only one operand allowed");
      // B;C;D;E;H;L;(HL);A
         else if (LexT(Op1) == _Rb) *RamP++ = 0313, *RamP++ = Op0a | LexN(Op1);
      // (IX+Ds);(IY+Ds)
         else if (LexT(Op1) == _x(_Rx)) {
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
      switch (LexC(Value)) {
   // Pseudo-Opcode
         case _OpP: DoPseudo(Cmd); break;
   // Opcode.
         case _Op: DoOpcode(Cmd); break;
   // Anything else: error.
         default: Error("Illegal token");
      }
   }
}
