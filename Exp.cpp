// Expression parser and calculator.
#include "Cas.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static SymbolP ErrSymbol;
PatchListP LastPatch; // To patch the type for incomplete formulas.

// Indirect recursion.
static int32_t GetExp0(CommandP &Cmd);

// Get a symbol, number or bracket
static int32_t GetExp3(CommandP &Cmd) {
   int32_t Value = 0;
   switch (Cmd->Type) {
      case NumL: Value = Cmd->Value; break;
      case SymL: {
      // Dereference the symbol.
         SymbolP Sym = (SymbolP)Cmd->Value;
         Value = Sym->Value;
      // Mark it, if it is the first undefined symbol.
         if (!Sym->Defined && ErrSymbol == nullptr) ErrSymbol = Sym;
      }
      break;
      case OpL:
         if (Cmd->Value == '(') {
         // Skip '(', get the embedded expression and check for ')'.
            Cmd++, Value = GetExp0(Cmd);
            if (Cmd->Type != OpL || Cmd->Value != ')') Error("Closing bracket is missing");
            break;
         }
      default:
         Error("Illegal symbol in a formula");
   }
   Cmd++; // Skip value, symbol or bracket.
   return Value;
}

// Interpret a sign.
static int32_t GetExp2(CommandP &Cmd) {
   bool HasNeg = false, HasNot = false;
   if (Cmd->Type == OpL) switch (Cmd->Value) {
   // Skip the sign: negative, and tag it.
      case '-': Cmd++, HasNeg = true; break;
   // Skip the sign: positive.
      case '+': Cmd++; break;
   // Skip the sign: not, and tag it.
      case '!': Cmd++, HasNot = true; break;
   }
   int32_t Value = GetExp3(Cmd);
   if (HasNeg) Value = -Value; // Negative operator: negate.
   if (HasNot) Value = !Value; // Not operator: invert.
   return Value;
}

// Multiplications, etc.
static int32_t GetExp1(CommandP &Cmd) {
   int32_t Value = GetExp2(Cmd);
   while (Cmd->Type == OpL) switch (Cmd->Value) {
   // Skip the operator: multiply.
      case '*': Cmd++, Value *= GetExp2(Cmd); break;
   // Skip the operator: divide.
      case '/': Cmd++, Value /= GetExp2(Cmd); break;
   // Skip the operator: modulo.
      case '%': Cmd++, Value %= GetExp2(Cmd); break;
   // Skip the operator: and.
      case '&': Cmd++, Value &= GetExp2(Cmd); break;
      default: goto Break;
   }
Break:
   return Value;
}

// Addition, etc.
static int32_t GetExp0(CommandP &Cmd) {
   int32_t Value = GetExp1(Cmd);
   while (Cmd->Type == OpL) switch (Cmd->Value) {
   // Skip the operator: add.
      case '+': Cmd++, Value += GetExp1(Cmd); break;
   // Skip the operator: subtract.
      case '-': Cmd++, Value -= GetExp1(Cmd); break;
   // Skip the operator: inclusive or.
      case '|': Cmd++, Value |= GetExp2(Cmd); break;
   // Skip the operator: exclusive or.
      case '^': Cmd++, Value ^= GetExp2(Cmd); break;
   // Skip the operator: shift to the right.
      case 0x120: Cmd++, Value >>= GetExp2(Cmd); break;
   // Skip the operator: shift to the left.
      case 0x121: Cmd++, Value <<= GetExp2(Cmd); break;
      default: goto Break;
   }
Break:
   return Value;
}

// Calculate an expression.
int32_t GetExp(CommandP &Cmd) {
   CommandP Cmd0 = Cmd;
// Clear out the error markers.
   LastPatch = nullptr, ErrSymbol = nullptr;
   int32_t Value = GetExp0(Cmd);
   if (ErrSymbol != nullptr) { // Remedial action, if any subexpression was undefined.
   // Allocate and check for clear space for the expression and end-marker and populate it.
      int32_t Len = (long)Cmd - (long)Cmd0 + sizeof *Cmd;
      CommandP NewCmd = (CommandP)calloc(1, Len); if (NewCmd == nullptr) exit(1);
      memcpy(NewCmd, Cmd0, (long)Cmd - (long)Cmd0);
   // Allocate a recalculation list entry.
      PatchListP Patch = (PatchListP)malloc(sizeof *Patch);
   // Link it to the expression, with an initially unknown type and zeroed out patch address.
      Patch->Cmd = NewCmd, Patch->Type = -1, Patch->Addr = 0;
   // Link expression to the symbol and save the entry to correct the type.
      Patch->Next = ErrSymbol->Patch, LastPatch = ErrSymbol->Patch = Patch;
   }
   return Value;
}
