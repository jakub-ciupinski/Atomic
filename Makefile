RACK_DIR ?= ./Rack-SDK

# The SDK found, C++17 and the macOS workarounds, in a file of their own so
# that any other plugin built from this tree shares them.
include sdk.mk

# Two forms are built from this tree. A plain make (make dist, make install)
# is the release form, the one the VCV Library builds: the dev modules' sources
# are left out and their models never registered, and plugin.json, the release
# manifest, does not list them. The dev form adds the dev modules (the
# harness's rig and NUDGE): `make dev` builds it and packs it as dist does,
# with their manifest entries (plugin.dev.json) added to the package's
# plugin.json, and `make dev-install` installs it. dev.sh and every loop build
# that. A dev module is named twice, by source here and in plugin.dev.json.
ATOMIC_DEV ?= 0
DEV_SOURCES := src/Harness.cpp src/Nudge.cpp
ifeq ($(ATOMIC_DEV),1)
EXTRA_FLAGS += -DATOMIC_DEV
SOURCES += $(wildcard src/*.cpp)
else
SOURCES += $(filter-out $(DEV_SOURCES),$(wildcard src/*.cpp))
endif

DISTRIBUTABLES += res
DISTRIBUTABLES += window.json
DISTRIBUTABLES += $(wildcard LICENSE*)
# The factory rigs, shipped with the plugin and never written (src/Rig.cpp).
DISTRIBUTABLES += rigs

include $(RACK_DIR)/plugin.mk

# The objects remember which form they were built for, so a switch from one
# to the other rebuilds them rather than linking the other form's plugin.cpp.
# The form is written into build/form and compared by content when the
# Makefile is read, and a switch deletes the other form's objects there, before
# make has judged any target: a marker compared by age let objects of the other
# form, newer than it, link into a mixed plugin, and a marker rule deleting
# them while make ran let the link look for objects it had already judged
# built. The goals that only call make again, or build or name nothing here,
# leave the build alone.
ifeq ($(filter-out dev dev-install tools tools-dist tools-install tools-clean distributables,$(or $(MAKECMDGOALS),all)),)
else ifneq ($(shell cat build/form 2>/dev/null),$(ATOMIC_DEV))
$(shell rm -rf build/src build/form-*; mkdir -p build; echo $(ATOMIC_DEV) > build/form)
endif

# The dev form's package: dist packs the repo's plugin.json, so the dev
# modules' entries are added to the packed copy and the package is packed
# again, as dist packs it.
.PHONY: dev dev-install
dev:
	$(MAKE) ATOMIC_DEV=1 dist
	jq '.modules += input' plugin.json plugin.dev.json > dist/$(SLUG)/plugin.json
	rm -f dist/*.vcvplugin
	cd dist && tar --no-xattrs -c $(SLUG) | zstd -q -$(ZSTD_COMPRESSION_LEVEL) -o "$(SLUG)"-"$(VERSION)"-$(ARCH_NAME).vcvplugin
dev-install: dev
	mkdir -p "$(PLUGINS_DIR)"
	cp dist/*.vcvplugin "$(PLUGINS_DIR)"/

# The workshop's tools plugin, where the checkout has one (the public
# repository does not): every loop that needs it in a Rack builds it with
# these.
ifneq ($(wildcard tools/Makefile),)
.PHONY: tools tools-dist tools-install tools-clean
tools:
	$(MAKE) -C tools
tools-dist:
	$(MAKE) -C tools dist
tools-install:
	$(MAKE) -C tools install
tools-clean:
	$(MAKE) -C tools clean
endif

# What the package carries, for release.sh to stage exactly that.
.PHONY: distributables
distributables:
	@echo $(DISTRIBUTABLES)
