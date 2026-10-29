# Root only, after reviewed source adoption. No Java compilation here.
# GNU Make4.4 owns ONE three-slot FIFO shared by both Ninja1.13 clients.
ifeq ($(filter 4.4%,$(MAKE_VERSION)),)
$(error GNU Make4.4 is required)
endif
ATTEMPT ?= r1
TASK := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
unexport CMAKE_BUILD_PARALLEL_LEVEL
.PHONY: all clang gcc san
all: gcc san
gcc san: clang
clang:
	+@python3 "$(TASK)/run.py" clang --attempt "$(ATTEMPT)"
gcc:
	+@python3 "$(TASK)/run.py" gcc --attempt "$(ATTEMPT)"
san:
	+@python3 "$(TASK)/run.py" san --attempt "$(ATTEMPT)"
