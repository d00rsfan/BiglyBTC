#!/usr/bin/env python3
"""Run provider checks against explicitly built modules and synthetic data."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys

test, fixture_module, location_module, mmdb = map(Path, sys.argv[1:5])
environment = os.environ.copy()
environment['LD_LIBRARY_PATH'] = str(location_module.parent) + os.pathsep + environment.get('LD_LIBRARY_PATH', '')
for prefix, module in [('PROVIDER', fixture_module), ('LOCATION', location_module)]:
    environment['BIGLYBTC_COUNTRY_' + prefix + '_TEST_MODULE'] = str(module)
    environment['BIGLYBTC_COUNTRY_' + prefix + '_TEST_SHA256'] = hashlib.sha256(module.read_bytes()).hexdigest()
environment['BIGLYBTC_COUNTRY_LOCATION_TEST_VERSION'] = '0.1.0'
environment['BIGLYBTC_COUNTRY_MMDB_TEST_FIXTURE'] = str(mmdb)
sys.exit(subprocess.call([str(test), *sys.argv[5:]], env=environment))
