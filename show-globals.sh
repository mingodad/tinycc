#!/bin/sh

dump_globals() {
	gdb -batch -ex "info variables" -ex quit --args ./$1 > globals-$1.txt
}

dump_globals tcc
dump_globals arm-tcc
dump_globals arm-wince-tcc
dump_globals arm64-osx-tcc
dump_globals arm64-tcc
dump_globals c67-tcc
dump_globals i386-tcc
dump_globals i386-win32-tcc
dump_globals riscv64-tcc
dump_globals x86_64-tcc
dump_globals x86_64-osx-tcc
dump_globals x86_64-win32-tcc

