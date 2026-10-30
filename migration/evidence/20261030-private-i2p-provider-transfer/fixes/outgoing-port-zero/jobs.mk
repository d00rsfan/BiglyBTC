TASK := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
.PHONY: all
all:
	+@python3 -B "$(TASK)/build.py"
