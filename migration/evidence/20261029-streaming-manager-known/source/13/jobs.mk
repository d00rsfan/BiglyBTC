# Root-only affected M suites; retained rows stay explicitly associated.
ifeq ($(filter 4.4%,$(MAKE_VERSION)),)
$(error GNU Make4.4 FIFO jobserver is required)
endif
ifndef SOURCE_COMMIT
$(error Supply actual SOURCE_COMMIT)
endif
ifndef ADOPTION
$(error Supply actual absolute fixture root ADOPTION)
endif
TASK := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ATTEMPT ?= a2
unexport CMAKE_BUILD_PARALLEL_LEVEL
.PHONY: all clang gcc san
all: gcc san
gcc san: clang
clang gcc san:
	+@python3 -B "$(TASK)/run.py" "$@" --attempt "$(ATTEMPT)" --source-commit "$(SOURCE_COMMIT)" --adoption "$(ADOPTION)"
