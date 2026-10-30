# Root-only one FIFO3; private Clang first, then defaultGCC/privateGCC/SAN.
ifeq ($(filter 4.4%,$(MAKE_VERSION)),)
$(error GNU Make4.4 FIFO jobserver required)
endif
TASK := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ATTEMPT ?= r2
unexport CMAKE_BUILD_PARALLEL_LEVEL
.PHONY: all clang default gcc san
all: default gcc san
default gcc san: clang
clang default gcc san:
	+@python3 -B "$(TASK)/run.py" "$@" --attempt "$(ATTEMPT)" --source-commit "$(SOURCE_COMMIT)" --adoption "$(ADOPTION)" --provider-record "$(PROVIDER_RECORD)" --provider-approval "$(PROVIDER_APPROVAL)" --provider-approval-sha256 "$(PROVIDER_APPROVAL_SHA256)" --provider-runtime-manifest "$(PROVIDER_RUNTIME_MANIFEST)" --provider-review "$(PROVIDER_REVIEW)"
