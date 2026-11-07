HERE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ATTEMPT ?= r1
.PHONY: all gcc san
all: san
gcc:
	+/usr/bin/python3 -I -B "$(HERE)run.py" gcc --attempt "$(ATTEMPT)"
san: gcc
	+/usr/bin/python3 -I -B "$(HERE)run.py" san --attempt "$(ATTEMPT)"
