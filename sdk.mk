# What every plugin built from this repo shares before the SDK's own plugin.mk:
# the SDK found, C++17, and the macOS workarounds. Included by the Makefile at
# the root (Atomic) and by any other plugin's Makefile in the tree, each with
# RACK_DIR set to the Rack-SDK symlink at the root.

# Rack-SDK is a gitignored symlink, so a fresh checkout has no SDK at all and
# make would otherwise fail on a missing include with nothing to act on.
ifeq ($(wildcard $(RACK_DIR)/plugin.mk),)
$(error No Rack SDK at $(RACK_DIR). It is a gitignored symlink at the root of \
  the repo; re-make it with  ln -s /path/to/Rack-SDK Rack-SDK  (or set RACK_DIR). \
  A matching SDK for this Rack version and architecture is at vcvrack.com/downloads)
endif

# The SDK defaults to C++11; this plugin uses C++17. EXTRA_* land after the
# SDK's own flags, so these win.
EXTRA_CXXFLAGS += -std=c++17

# Everything below is for a mac target, chosen by the target the SDK's arch.mk
# works out (CROSS_COMPILE, else the compiler's own), never by the host, so a
# Windows or Linux build made on a Mac gets the SDK's own flags untouched.
# arch.mk is the SDK's and is included again by plugin.mk, harmlessly.
include $(RACK_DIR)/arch.mk
ifdef ARCH_MAC

# The SDK asks for a 10.9 deployment target. An arm64 Mac starts at macOS 11,
# and Xcode ships no arm64 libc++ for anything older, so arm64 says 11.0; an
# x64 build keeps the SDK's 10.9, which the code compiles clean against, so an
# Intel Mac on an older macOS still loads the plugin.
ifdef ARCH_ARM64
EXTRA_FLAGS += -mmacosx-version-min=11.0
EXTRA_LDFLAGS += -mmacosx-version-min=11.0
endif

# Some Command Line Tools installs carry a truncated copy of the libc++
# headers in usr/include/c++/v1, which clang prefers over the complete set
# inside the SDK, so <cstddef> and friends go missing. Point the compiler at
# the SDK copy instead. This one is the host's own install, so it applies only
# on a Mac host, and disables itself once a full CLT is present.
ifeq ($(shell uname),Darwin)
MACOS_SDK := $(shell xcrun --show-sdk-path 2>/dev/null)
ifeq ($(wildcard /Library/Developer/CommandLineTools/usr/include/c++/v1/cstddef),)
ifneq ($(wildcard $(MACOS_SDK)/usr/include/c++/v1/cstddef),)
EXTRA_CXXFLAGS += -nostdinc++ -isystem $(MACOS_SDK)/usr/include/c++/v1
endif
endif
endif

endif
