CC=gcc
# CC=clang
RM=rm -f

CFLAGS=-I. -Wall

DEPS = z80_assembler.h kk_ihex_read.h kk_ihex_write.h Makefile

%.o: %.cpp $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)
%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: z80assembler z80disassembler
z80assembler: z80_assembler.o z80_tokenize.o z80_compile.o z80_calc.o kk_ihex_write.o
	$(CC) -o $@ $^ $(CFLAGS)
z80disassembler: z80_disassembler.o kk_ihex_read.o
	$(CC) -o $@ $^ $(CFLAGS)

Z80.s:	Z80.bin z80disassembler
	./z80disassembler Z80.bin Z80.s
Z80.hex: Z80.asm z80assembler
	./z80assembler Z80.asm

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
	$(RM) z80assembler
	$(RM) z80disassembler
