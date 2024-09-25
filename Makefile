CC=g++
# CC=clang
RM=rm -f

CFLAGS=-I. ## -Wall

DEPS = Cas.h HexIn.h HexEx.h Makefile

%.o: %.cpp $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: CasZ80 DasZ80
CasZ80: Cas.o Lex.o Syn.o Exp.o HexEx.o
	$(CC) -o $@ $^ $(CFLAGS)
DasZ80: Das.o HexIn.o
	$(CC) -o $@ $^ $(CFLAGS)

Z80.s:	Z80.bin DasZ80
	./DasZ80 Z80.bin Z80.s
Z80.hex: Z80.asm CasZ80
	./CasZ80 Z80.asm

detest: Z80.s
	diff Z80.s Z80.asm
entest: Z80.hex
	diff Z80.hex Z80.en
test: detest entest

clean:
	$(RM) *.o
cleantest:
	$(RM) Z80.s
	$(RM) Z80.hex
	$(RM) Z80.z80
clobber: clean cleantest
	$(RM) CasZ80
	$(RM) DasZ80
