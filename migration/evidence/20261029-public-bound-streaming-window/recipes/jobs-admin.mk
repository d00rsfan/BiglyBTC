# Root-only; prepare one fresh helper reference first. No javac inside this pool.
ifeq ($(filter 4.4%,$(MAKE_VERSION)),)
$(error GNU Make4.4 FIFO jobserver is required)
endif
ifndef SOURCE_COMMIT
$(error Supply actual adopted SOURCE_COMMIT)
endif
ifndef ADOPTION
$(error Supply actual absolute ADOPTION receipt)
endif
ifndef REFERENCE
$(error Supply actual prepare.py result REFERENCE)
endif
ifndef NAMESPACE_PREFLIGHT
$(error Supply actual reviewed NAMESPACE_PREFLIGHT outer result)
endif
ATTEMPT ?= admin-r1
TASK := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
unexport CMAKE_BUILD_PARALLEL_LEVEL
.PHONY: all clang gcc san
all: gcc san
gcc san: clang
clang:
	+@python3 -B "$(TASK)/run-admin.py" clang --attempt "$(ATTEMPT)" --source-commit "$(SOURCE_COMMIT)" --adoption "$(ADOPTION)" --reference "$(REFERENCE)" --namespace-preflight "$(NAMESPACE_PREFLIGHT)"
gcc:
	+@python3 -B "$(TASK)/run-admin.py" gcc --attempt "$(ATTEMPT)" --source-commit "$(SOURCE_COMMIT)" --adoption "$(ADOPTION)" --reference "$(REFERENCE)" --namespace-preflight "$(NAMESPACE_PREFLIGHT)"
san:
	+@python3 -B "$(TASK)/run-admin.py" san --attempt "$(ATTEMPT)" --source-commit "$(SOURCE_COMMIT)" --adoption "$(ADOPTION)" --reference "$(REFERENCE)" --namespace-preflight "$(NAMESPACE_PREFLIGHT)"
