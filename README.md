# rge_fio

Small commandline tool that deflates input files exactly the same way Age of Empires does (at least up to HD).

Uses zlib to inflate input files, and optionally even though it kind of defeats the point of this program, to deflate as well (check deflate.c).

Its unaltered source (some files are removed, but otherwise zlib is unchanged) is found in the folder "zlib".

## usage

The usage is simple:

    rge_fio r input.zlib output.dump

inflates (r for reading) `input.zlib` and stores the output in `output.dump`.

Additionally

    rge_fio r input.zlib output.dump 4

reads and writes the first 4 byte uncompressed, and decompresses the rest of the input (discarding any trailing data for now).

Even more additionally

    rge_fio r input.zlib output.dump 4 28

seeks to position 28 in the input file, reads and writes the next 4 byte uncompressed, and decompresses the rest of the input (discarding any trailing data for now).

The same works for writing.

## building

Run `./premake5 gmake` on MSYS2 or Unix, `cd build`, `make`.

For Visual Studio just hit the `premake-vs2019.cmd` and use the .sln generated in `build`.
