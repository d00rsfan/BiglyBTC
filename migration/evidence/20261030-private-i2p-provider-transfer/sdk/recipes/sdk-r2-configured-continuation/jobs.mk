ifeq ($(filter 4.4%,$(MAKE_VERSION)),)
$(error GNU Make4.4 is required)
endif
ifeq ($(strip $(OUTPUT)),)
$(error Supply OUTPUT=/absolute/fresh/provider-candidate-r2)
endif
ATTEMPT ?= r3
TASK := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
unexport CMAKE_BUILD_PARALLEL_LEVEL
.PHONY: all
all:
	+@python3 "$(TASK)/provider.py" build --output "$(OUTPUT)" --attempt "$(ATTEMPT)"
