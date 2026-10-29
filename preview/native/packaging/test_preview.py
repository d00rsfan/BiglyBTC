#!/usr/bin/env python3
"""Exercise actual launcher child processes; no Qt or desktop is required."""

import os
from pathlib import Path
import subprocess
import sys
import unittest


WARNING = (b"qt.qpa.wayland.textinput: virtual void "
           b"QtWaylandClient::QWaylandTextInputv3::disableSurface(wl_surface*) "
           b"Trying to disable 0x5704bc83ede0 but 0x0 is focused\n")
DRIVER = """
import os, runpy, sys
preview = runpy.run_path(sys.argv[1])
raise SystemExit(preview['run_preview']([sys.executable, '-c', sys.argv[2]], os.environ.copy()))
"""


class PreviewConsoleTests(unittest.TestCase):
    def run_child(self, stderr, *, environment=None, exit_code=7):
        env = os.environ.copy()
        for key in ("BIGLYBTC_WAYLAND_DIAGNOSTICS", "QT_FATAL_WARNINGS",
                    "QT_MESSAGE_PATTERN", "QT_LOGGING_RULES", "QT_LOGGING_CONF"):
            env.pop(key, None)
        env.update(environment or {})
        child = ("import sys; sys.stdout.buffer.write(b'preview stdout\\n'); "
                 f"sys.stderr.buffer.write({stderr!r}); raise SystemExit({exit_code})")
        result = subprocess.run(
            [sys.executable, "-c", DRIVER, str(Path(__file__).with_name("preview.py")), child],
            env=env, capture_output=True, check=False, timeout=10)
        self.assertEqual(result.returncode, exit_code)
        self.assertEqual(result.stdout, b"preview stdout\n")
        return result.stderr

    def test_redundant_idle_disable_is_removed_without_losing_other_output(self):
        before, after = b"startup\n", b"actual error: \xff\x00\nunterminated error"
        second = WARNING.replace(b"5704bc83ede0", b"5704bc6e49b0")
        self.assertEqual(self.run_child(before + WARNING + second + after), before + after)

    def test_other_focus_and_protocol_warnings_are_preserved(self):
        messages = (WARNING.replace(b"but 0x0", b"but 0x1234")
                    + WARNING.replace(b"is focused", b"is enabled")
                    + WARNING.replace(b"disableSurface", b"enableSurface")
                    + WARNING.replace(b"qt.qpa.wayland.textinput", b"another.category")
                    + b"qt.qpa.wayland.textinput: Unexpected text input action received\n")
        self.assertEqual(self.run_child(messages), messages)

    def test_debug_opt_in_preserves_the_full_diagnostic(self):
        self.assertEqual(self.run_child(WARNING, environment={"BIGLYBTC_WAYLAND_DIAGNOSTICS": "1"}), WARNING)

    def test_fatal_warning_mode_keeps_the_triggering_warning_visible(self):
        self.assertEqual(self.run_child(WARNING, environment={"QT_FATAL_WARNINGS": "1"}), WARNING)

    def test_explicit_qt_logging_controls_preserve_the_diagnostic(self):
        for name in ("QT_MESSAGE_PATTERN", "QT_LOGGING_RULES", "QT_LOGGING_CONF"):
            with self.subTest(name=name):
                self.assertEqual(self.run_child(WARNING, environment={name: ""}), WARNING)

    def test_custom_format_and_clean_child_exit_are_preserved(self):
        formatted = b"warning: " + WARNING
        self.assertEqual(self.run_child(formatted, exit_code=0), formatted)


if __name__ == "__main__":
    unittest.main()
