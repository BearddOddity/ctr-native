#!/bin/bash
# Build the PSX matching toolchain as native Windows programs, from the vendored sources, in an MSYS2 shell.
#
#     C:\msys64\usr\bin\bash.exe -lc "cd /d/path/to/ctr-native && tools/matching/windows/build-toolchain.sh"
#
# Needs these MSYS2 packages (once):
#     pacman -S --needed make patch bison flex diffutils texinfo mingw-w64-i686-gcc
#
# GCC 2.8.1 is built for a 32-bit Windows host (i386-pc-mingw32), as the Linux toolchain is built for a 32-bit Linux
# host, with exactly the patches externals/old-gcc/gcc-2.8.1-psx.Dockerfile applies: the patch steps are read from that
# Dockerfile, so the two cannot drift apart. binutils 2.40 is built from externals/psx-binutils for mipsel-none-elf.
# Everything lands in externals/old-gcc/prebuilt/windows-i386/2.8.1 and externals/psx-binutils/prebuilt/windows-i386.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
WORK="$ROOT/build/toolchain-win"
GCC_OUT="$ROOT/externals/old-gcc/prebuilt/windows-i386/2.8.1"
BINUTILS_OUT="$ROOT/externals/psx-binutils/prebuilt/windows-i386"
export PATH="/mingw32/bin:/usr/bin:$PATH"
JOBS="${JOBS:-$(nproc)}"

mkdir -p "$WORK"

build_gcc() {
	local src="$WORK/gcc-2.8.1"
	rm -rf "$src"
	tar -xzf "$ROOT/externals/old-gcc/gcc-2.8.1.tar.gz" -C "$WORK"
	echo '3b30fbfdf93e628373d90d174243f3267b0eec9ebe792bb64fd15b8828c2ea4c  '"$ROOT/externals/old-gcc/gcc-2.8.1.tar.gz" | sha256sum -c -
	rm -rf "$WORK/patches"
	cp -r "$ROOT/externals/old-gcc/patches" "$WORK/patches"

	# The Dockerfile's RUN steps between copying the patches and configuring, run here as they are written.
	local steps="$WORK/gcc-2.8.1-patch-steps.sh"
	awk '/^COPY patches/ {on=1; next} /^RUN \.\/configure/ {on=0} on && /^RUN / {sub(/^RUN /, ""); print}' \
		"$ROOT/externals/old-gcc/gcc-2.8.1-psx.Dockerfile" >"$steps"
	(cd "$src" && bash -euo pipefail "$steps")
	# Windows only: the driver starts cpp and cc1 through _spawnv, which quotes no argument, so
	# -D__SIZE_TYPE__=unsigned int arrived as two. This changes how the driver runs programs, not what cc1 emits.
	(cd "$src" && chmod u+w pexecute.c && patch --batch --forward --fuzz=0 -u -p1 \
		<"$ROOT/tools/matching/windows/pexecute-quote-args-2.8.1.patch")

	# GCC 2.8.1's configure tests with K&R C (`main(){return(0);}`), which a current GCC rejects unless told it is
	# C89; MSYS2's config.site would otherwise pick a 64-bit build.
	(cd "$src" && CONFIG_SITE=/dev/null CC=gcc CFLAGS="-std=gnu89 -O2 -fcommon" ./configure \
		--target=mips-sony-psx \
		--prefix=/opt/cross \
		--with-endian-little \
		--with-gnu-as \
		--disable-gprof \
		--disable-gdb \
		--disable-werror \
		--host=i386-pc-mingw32 \
		--build=i386-pc-mingw32)
	touch "$src/insn-config.h"
	(cd "$src" && make --jobs="$JOBS" cc1 cpp xgcc CC=gcc CFLAGS="-std=gnu89 -O2 -static -fcommon")

	mkdir -p "$GCC_OUT"
	cp "$src/cc1.exe" "$GCC_OUT/cc1.exe"
	cp "$src/cpp.exe" "$GCC_OUT/cpp.exe"
	cp "$src/xgcc.exe" "$GCC_OUT/gcc.exe"
}

build_binutils() {
	local src="$WORK/binutils-2.40"
	local obj="$WORK/binutils-build"
	rm -rf "$src" "$obj"
	tar -xJf "$ROOT/externals/psx-binutils/binutils-2.40.tar.xz" -C "$WORK"
	mkdir -p "$obj"
	# binutils 2.40 defines its own static_assert, which is a keyword in C23 (a current GCC's default): build as C17.
	# The host is given explicitly: guessed, it comes out as a 64-bit MinGW (or MSYS) host that does not match the
	# 32-bit compiler, and BFD then writes object files in text mode, turning every 0x0a byte into 0x0d 0x0a.
	(cd "$obj" && CONFIG_SITE=/dev/null CC=gcc CFLAGS="-O2 -std=gnu17" "$src/configure" \
		--target=mipsel-none-elf \
		--host=i686-w64-mingw32 \
		--build=i686-w64-mingw32 \
		--prefix=/opt/psx-binutils \
		--disable-nls \
		--disable-werror \
		--disable-gdb \
		--disable-gprof \
		--disable-sim \
		--disable-plugins \
		--enable-static \
		--disable-shared)
	(cd "$obj" && make --jobs="$JOBS" all-gas all-ld all-binutils LDFLAGS="-static")

	mkdir -p "$BINUTILS_OUT/bin"
	cp "$obj/gas/as-new.exe" "$BINUTILS_OUT/bin/mipsel-none-elf-as.exe"
	cp "$obj/ld/ld-new.exe" "$BINUTILS_OUT/bin/mipsel-none-elf-ld.exe"
	# The same four programs the Linux toolchain keeps (see externals/psx-binutils/UPSTREAM.md).
	cp "$obj/binutils/objdump.exe" "$BINUTILS_OUT/bin/mipsel-none-elf-objdump.exe"
	cp "$obj/binutils/objcopy.exe" "$BINUTILS_OUT/bin/mipsel-none-elf-objcopy.exe"
}

case "${1:-all}" in
gcc) build_gcc ;;
binutils) build_binutils ;;
all)
	build_gcc
	build_binutils
	;;
*)
	echo "usage: $0 [gcc|binutils|all]" >&2
	exit 2
	;;
esac
echo "done"
