# dmask286

I recently had the displeasure of analyzing some old
old Intel hex dumps with the standard objdump utility,
so I did what anyone in my position would do - 
write a disassembler so I could take a look
myself! It's also something I can easily integrate into
an emulator and put a monitor on top.

This only covers 286 instructions (+FPU), nothing more.
I used the "80286 and 80287 Programmer's Reference Manual"
as a basis (ISBN 1-55512-055-5).
The LOADALL286 instruction is added as well.

Some stuff may be wrong or incorrect.

The code is quite simple so you may get some insight 
by reading a bit of it. The output should be:
addr: hexbytes ; mnemonic [operands]

The mnemonics are similar but not equivalent to the ones
in nasm. The operands are printed _as is_ and not
show relative like in objdump, which is important for
my use-case. I am using this to build a simple monitor to
specify the bytes I want directly - mnemonics are too prone to
interpretation to consistently get what I want, especially
since the sizes of the operations emitted sometimes change as
well, which screws up a lot of later jumps in the code.

# Dependencies
You need gcc to compile the main dmask286 program. In addition
nasm is required for the tests.
