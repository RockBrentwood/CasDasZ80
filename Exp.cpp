// Calculate a formula
#include "Cas.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static SymbolP ErrSymbol;
PatchListP LastPatch; // to patch the type for incomplete formulas

// Indirect recursion.
static int32_t GetExp0(CommandP &Cmd);

// Get a symbol, number or bracket
static int32_t GetExp3(CommandP &Cmd) {
   int32_t Value = 0;
   switch (Cmd->Type) {
      case NumL:
         Value = Cmd->Value;
      break;
      case SymL: {
         SymbolP Sym = (SymbolP)Cmd->Value; // Symbol ptr is in the value
         Value = Sym->Value; // value of the symbol
         if (!Sym->Defined) { // is the symbol defined?
            if (!ErrSymbol) // Already an undefined symbol?
               ErrSymbol = Sym; // remember this symbol, if not
         }
      }
      break;
      case OpL:
         if (Cmd->Value == '(') {
            Cmd++; // Skip opening bracket
            Value = GetExp0(Cmd);
            if ((Cmd->Type != OpL) || (Cmd->Value != ')')) {
               Error("Closing bracket is missing");
            }
         } else
      default:
         Error("Illegal symbol in a formula");
   }
   Cmd++; // skip value, symbol or bracket
   return Value;
}

// interpret a sign
static int32_t GetExp2(CommandP &Cmd) {
   bool HasNeg = false;
   bool HasNot = false;
   if (Cmd->Type == OpL) {
      if (Cmd->Value == '-') {
         Cmd++; // skip the sign
         HasNeg = true; // negative operator detected
      } else if (Cmd->Value == '+') {
         Cmd++; // skip the sign
      } else if (Cmd->Value == '!') {
         Cmd++; // skip the sign
         HasNot = true; // NOT operator detected
      }
   }
   int32_t Value = GetExp3(Cmd);
   if (HasNeg) // negative operator?
      Value = -Value; // negate
   if (HasNot) // NOT operator?
      Value = !Value; // invertieren
   return Value;
}

// multiplications, etc.
static int32_t GetExp1(CommandP &Cmd) {
   bool DoBreak = false;
   int32_t Value = GetExp2(Cmd);
   while ((Cmd->Type == OpL) && !DoBreak) {
      switch (Cmd->Value) {
         case '*':
            Cmd++; // skip operator
            Value *= GetExp2(Cmd); // Multiply
         break;
         case '/':
            Cmd++; // skip operator
            Value /= GetExp2(Cmd); // Divide
         break;
         case '%':
            Cmd++; // skip operator
            Value %= GetExp2(Cmd); // Modulo
         break;
         case '&':
            Cmd++; // skip operator
            Value &= GetExp2(Cmd); // And operator
         break;
         default:
            DoBreak = true;
      }
   }
   return Value;
}

// addition, etc.
static int32_t GetExp0(CommandP &Cmd) {
   bool DoBreak = false;
   int32_t Value = GetExp1(Cmd);
   while ((Cmd->Type == OpL) && !DoBreak) {
      switch (Cmd->Value) {
         case '+':
            Cmd++; // skip operator
            Value += GetExp1(Cmd); // plus
         break;
         case '-':
            Cmd++; // skip operator
            Value -= GetExp1(Cmd); // minus
         break;
         case '|':
            Cmd++; // skip operator
            Value |= GetExp2(Cmd); // or
         break;
         case '^':
            Cmd++; // skip operator
            Value ^= GetExp2(Cmd); // Xor
         break;
         case 0x120:
            Cmd++; // skip operator
            Value >>= GetExp2(Cmd); // shift to the right
         break;
         case 0x121:
            Cmd++; // skip operator
            Value <<= GetExp2(Cmd); // shift to the left
         break;
         default:
            DoBreak = true;
      }
   }
   return Value;
}

// Calculate a formula
int32_t GetExp(CommandP &Cmd) {
   CommandP Cmd0 = Cmd;
   LastPatch = nullptr; // expression so far ok
   ErrSymbol = nullptr; // no undefined symbol in formula
   int32_t Value = GetExp0(Cmd);
   if (ErrSymbol) { // at least one symbol is undefined?
      int32_t Len = (long)Cmd - (long)Cmd0 + sizeof(Command); // space for the formula and end-marker
      CommandP NewCmd = (CommandP)malloc(Len); // allocate memory for the formular
      if (!NewCmd)
         exit(1); // not enough memory
      memset(NewCmd, 0, Len); // erase memory
      memcpy(NewCmd, Cmd0, (long)Cmd - (long)Cmd0); // transfer the formular
      PatchListP Patch = (PatchListP)malloc(sizeof(PatchList)); // allocate a recalculation list entry
      Patch->Cmd = NewCmd; // link to the formula
      Patch->Type = -1; // Type: illegal (because unknown)
      Patch->Addr = 0; // address to patch = 0
      Patch->Next = ErrSymbol->Patch;
      ErrSymbol->Patch = Patch; // link expression to symbol
      LastPatch = Patch; // save entry to correct the type
   }
   return Value;
}
