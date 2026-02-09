.PHONY: clean all diff test

CXXFLAGS ?= -Os

all: dmask286

%.COM: %.nasm
	nasm -w-prefix-lock-error -O0 -f bin $^ -o $@

dmask286: dmask.cpp File.cpp
	$(CXX) -std=gnu++17 -Wall -Wextra $(CXXFLAGS) $(CXXEXTFLAGS) $^ -o $@

clean:
	$(RM) *.COM dmask286 *.temp compile_commands.*

test: dmask286 test.COM testf.COM callback.COM testlen.COM testlen2.COM
	./dmask286 test.COM > test.dasm.temp
	./dmask286 testf.COM > testf.dasm.temp
	./dmask286 callback.COM > callback.dasm.temp
	./dmask286 testlen.COM > testlen.dasm.temp
	./dmask286 testlen2.COM > testlen2.dasm.temp
	
	diff test.dasm test.dasm.temp
	diff testf.dasm testf.dasm.temp
	diff callback.dasm callback.dasm.temp
	diff testlen.dasm testlen.dasm.temp
	diff testlen2.dasm testlen2.dasm.temp
