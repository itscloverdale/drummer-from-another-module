RACK_DIR ?= $(HOME)/dev/Rack-SDK

FLAGS +=
CFLAGS +=
CXXFLAGS +=
LDFLAGS +=

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*) README.md CHANGELOG.md

include $(RACK_DIR)/plugin.mk

# Regenerates the panel and src/layout.hpp (needs the .venv with fonttools).
panel:
	.venv/bin/python tools/make_panel.py --preview

test:
	./tests/run.sh

.PHONY: panel test
