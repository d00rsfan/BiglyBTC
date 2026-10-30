ifeq ($(filter 4.4%,$(MAKE_VERSION)),)
$(error GNU Make4.4 FIFO jobserver required)
endif
TASK := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OUTPUT := /home/yurii/Java/BiglyBT/build/i2p-provider-attachment-20261029/provider-candidate-r1
ATTEMPT ?= r2
unexport CMAKE_BUILD_PARALLEL_LEVEL
.PHONY: all
all:
	+@python3 -B "$(TASK)/repair.py" build --output "$(OUTPUT)" --attempt "$(ATTEMPT)"
