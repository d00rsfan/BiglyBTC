# Root only, after javac-once and actual source adoption. No recursive make.
# GNU Make4.4 owns ONE three-slot FIFO shared by both Ninja1.13 clients.
ifeq ($(filter 4.4%,$(MAKE_VERSION)),)
$(error GNU Make4.4 is required)
endif
ifeq ($(strip $(CLASSES)),)
$(error Supply CLASSES=/absolute/freshly-compiled-two-class-directory)
endif
ATTEMPT ?= r2
TASK := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
unexport CMAKE_BUILD_PARALLEL_LEVEL
.PHONY: all clang gcc san
all: gcc san
gcc san: clang
clang:
	+@python3 "$(TASK)/run-r2.py" clang --attempt "$(ATTEMPT)" --classes "$(CLASSES)" --actor
gcc:
	+@python3 "$(TASK)/run-r2.py" gcc --attempt "$(ATTEMPT)" --classes "$(CLASSES)"
san:
	+@python3 "$(TASK)/run-r2.py" san --attempt "$(ATTEMPT)" --classes "$(CLASSES)" --actor
