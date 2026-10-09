# Build the code blob that goes into the CDJ-900 MAIN padding.
CC      = toolchain/bin/sh-elf-gcc
OBJCOPY = sh-elf-objcopy
NM      = sh-elf-nm
CFLAGS  = -m4-nofpu -ml -mdiv=call-div1 -Os -ffreestanding -fno-builtin -nostdlib \
          -ffunction-sections -fdata-sections -fno-common -Wall -Wextra \
          -Wno-unused-parameter -Isrc/libc -DCDJ_TARGET -mrenesas
SRCS    = src/fl_cmd.c src/fltest_core.c src/putfmt.c src/pool.c src/flac_impl.c src/rt.c
ASMS    = src/stack.S
OBJS    = $(SRCS:src/%.c=build/obj/%.o) $(ASMS:src/%.S=build/obj/%.o)
FLAC_SRCS = src/vw_cmd.c src/vp_cmd.c src/vfs_hook.c src/vh_stats.c src/vwav.c src/vwcheck.c src/crc32.c src/putfmt.c \
            src/pool.c src/flac_impl.c src/rt.c
FLAC_OBJS = $(FLAC_SRCS:src/%.c=build/obj/%.o) build/obj/stack.o
# The release blob: the hooks only, no console commands, no statistics.
REL_SRCS  = src/vfs_hook.c src/vwav.c src/pool.c src/flac_impl.c src/rt.c
REL_OBJS  = $(REL_SRCS:src/%.c=build/obj-rel/%.o) build/obj-rel/stack.o

all: build/blob.bin build/blob.sym build/host_fltest build/flac.bin build/flac.sym \
     build/flac-release.bin build/flac-release.sym \
     build/host_vwav build/test_vfs_hook

# DECODER_OPT: an extra -O flag for dr_flac only (the last -O flag wins).
build/obj/flac_impl.o build/obj-rel/flac_impl.o: CFLAGS += $(DECODER_OPT)

# Debug objects (build/obj) have the statistics; release objects do not.
build/obj/%.o: src/%.c src/*.h third_party/dr_flac.h
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -DFLAC_STATS -c $< -o $@

build/obj-rel/%.o: src/%.c src/*.h third_party/dr_flac.h
	@mkdir -p build/obj-rel
	$(CC) $(CFLAGS) -c $< -o $@

build/obj-rel/%.o: src/%.S
	@mkdir -p build/obj-rel
	$(CC) $(CFLAGS) -c $< -o $@

build/blob.elf: $(OBJS) src/link.ld
	$(CC) $(CFLAGS) -T src/link.ld -Wl,--gc-sections -o $@ $(OBJS) -lgcc

build/blob.bin: build/blob.elf
	$(OBJCOPY) -O binary $< $@

build/blob.sym: build/blob.elf
	$(NM) -n $< > $@

build/flac.elf: $(FLAC_OBJS) src/flac.ld
	$(CC) $(CFLAGS) -T src/flac.ld -Wl,--gc-sections -o $@ $(FLAC_OBJS) -lgcc

build/flac.bin: build/flac.elf
	$(OBJCOPY) -O binary $< $@

build/flac.sym: build/flac.elf
	$(NM) -n $< > $@

build/flac-release.elf: $(REL_OBJS) src/flac-release.ld
	$(CC) $(CFLAGS) -T src/flac-release.ld -Wl,--gc-sections -o $@ $(REL_OBJS) -lgcc

build/flac-release.bin: build/flac-release.elf
	$(OBJCOPY) -O binary $< $@

build/flac-release.sym: build/flac-release.elf
	$(NM) -n $< > $@

build/host_fltest: tests/host_fltest.c src/fltest_core.c src/pool.c src/flac_impl.c
	@mkdir -p build
	cc -O2 -Wall -o $@ $^ -lm

VWAV_SRCS = src/vwav.c src/vwcheck.c src/crc32.c src/pool.c src/flac_impl.c

build/host_vwav: tests/host_vwav.c $(VWAV_SRCS) src/*.h third_party/dr_flac.h
	@mkdir -p build
	cc -O2 -Wall -o $@ tests/host_vwav.c $(VWAV_SRCS) -lm

build/test_vfs_hook: tests/test_vfs_hook.c src/vfs_hook.c src/vh_stats.c src/putfmt.c $(VWAV_SRCS) src/*.h third_party/dr_flac.h
	@mkdir -p build
	cc -O2 -Wall -DFLAC_STATS -o $@ tests/test_vfs_hook.c src/vfs_hook.c src/vh_stats.c src/putfmt.c $(VWAV_SRCS) -lm

build/test_rt: tests/test_rt.c src/rt.c
	@mkdir -p build
	cc -O2 -Wall -fno-builtin -o $@ tests/test_rt.c

test: build/host_vwav build/test_vfs_hook build/abi_open build/test_rt build/webtest/.done
	python3 tests/test_vwav.py
	$(MAKE) test-web

# The PC tests without the SH-4 compiler (CI host job): the ABI test skips.
test-host: build/host_vwav build/test_vfs_hook build/test_rt build/webtest/.done
	CDJ_HOST_ONLY=1 python3 tests/test_vwav.py
	$(MAKE) test-web

ABI_SRCS = tests/abi/open_main.c src/vfs_hook.c src/vh_stats.c src/putfmt.c src/vwav.c src/vwcheck.c src/crc32.c \
           src/pool.c src/flac_impl.c src/rt.c

build/abi_open: $(ABI_SRCS) tests/abi/open_call.S tests/abi/link.ld src/stack.S src/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) -DFLAC_STATS -T tests/abi/link.ld -Wl,-z,max-page-size=0x1000 -o $@ $(ABI_SRCS) \
	    tests/abi/open_call.S src/stack.S -lgcc

check-abi: $(FLAC_SRCS)
	python3 tests/check_abi.py $(CC) $(CFLAGS) -DFLAC_STATS -- $(FLAC_SRCS)
	python3 tests/check_abi.py $(CC) $(CFLAGS) -- $(REL_SRCS)

# The browser patcher tests. The fake firmware needs no Pioneer files.
build/webtest/.done: tests/fake_firmware.py tools/upd.py tools/build_patch.py
	@mkdir -p build/webtest
	python3 tests/fake_firmware.py build/webtest
	touch $@

test-web: build/webtest/.done
	python3 tests/test_manifest.py build/webtest
	node --test tests/web/upd.test.mjs tests/web/zip.test.mjs tests/web/patcher.test.mjs tests/web/real.test.mjs

# The site for GitHub Pages: the page, and the manifest from the release blob.
site: build/flac-release.bin build/flac-release.sym
	rm -rf build/site
	mkdir -p build/site
	cp web/*.html web/*.js web/*.css build/site/
	python3 tools/build_patch.py manifest
	cp out/web/cdj900-4.32.json build/site/

# Local browser test (needs Playwright with Chromium).
PLAYWRIGHT_MODULE ?= $(shell dirname "$$(command -v playwright 2>/dev/null)" 2>/dev/null)/../playwright
test-browser: build/webtest/.done
	PLAYWRIGHT_MODULE=$(PLAYWRIGHT_MODULE) node --test tests/web/browser.test.mjs

clean:
	rm -rf build/obj build/obj-rel build/blob.* build/host_fltest build/flac.* build/host_vwav build/test_vfs_hook build/abi_open build/test_rt

.PHONY: all clean test test-host test-web check-abi site test-browser

build/obj/%.o: src/%.S
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -c $< -o $@
