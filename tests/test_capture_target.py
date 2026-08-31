#!/usr/bin/env python3
"""
Unit tests for tools/capture_target.py.
Exercises the deterministic tracker navigation and error-handling paths offline
using MockM8Driver without needing physical M8 hardware connected to COM3.
"""

import os
import sys
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

from capture_target import (
    MockM8Driver,
    NavigationError,
    TargetNavigator,
    parse_hex_or_int,
    fmt_hex,
    run_target_capture,
)


class TestCaptureTarget(unittest.TestCase):

    def setUp(self):
        self.mock = MockM8Driver()

    def test_parse_hex_and_format(self):
        self.assertEqual(parse_hex_or_int("0E"), 14)
        self.assertEqual(parse_hex_or_int("0x12"), 18)
        self.assertEqual(parse_hex_or_int("10"), 16)
        self.assertEqual(fmt_hex(14), "0E")
        self.assertEqual(fmt_hex(18), "12")

    def test_song_to_phrase_navigation_success(self):
        """Song Row 0, Track 2 -> Chain 0E -> Phrase 12."""
        ok = run_target_capture(
            song_row=0,
            track=2,
            phrase_id=0x12,
            scope="phrase",
            dry_run=True,
            driver_instance=self.mock,
        )
        self.assertTrue(ok)
        self.assertEqual(self.mock._screen, "PHRASE")
        self.assertEqual(self.mock.active_phrase, "12")

    def test_empty_song_cell_aborts(self):
        """Attempting to dive from an empty Song cell ('--') raises NavigationError."""
        with self.assertRaises(NavigationError) as ctx:
            run_target_capture(
                song_row=0,
                track=1,
                phrase_id=0x12,
                scope="phrase",
                dry_run=True,
                driver_instance=self.mock,
            )
        self.assertIn("is empty", str(ctx.exception))

    def test_missing_phrase_in_chain_aborts(self):
        """Looking for Phrase 99 in Chain 0E raises NavigationError."""
        with self.assertRaises(NavigationError) as ctx:
            run_target_capture(
                chain_id=0x0E,
                phrase_id=0x99,
                scope="phrase",
                dry_run=True,
                driver_instance=self.mock,
            )
        self.assertIn("not found in current CHAIN", str(ctx.exception))

    def test_direct_chain_to_phrase_scope_chain(self):
        """Direct Chain 0E -> target Phrase 11 with scope 'chain' parks on CHAIN."""
        self.mock.goto("SONG")
        ok = run_target_capture(
            chain_id=0x0E,
            phrase_id=0x11,
            scope="chain",
            dry_run=True,
            driver_instance=self.mock,
        )
        self.assertTrue(ok)
        self.assertEqual(self.mock._screen, "CHAIN")
        self.assertEqual(self.mock._grid_step, 1)  # Phrase 11 is at step 1 in Chain 0E

    def test_transport_stopped_before_navigation(self):
        """If device is playing, navigation must stop transport first."""
        self.mock._is_playing = True
        ok = run_target_capture(
            song_row=0,
            track=0,
            scope="song",
            dry_run=True,
            driver_instance=self.mock,
        )
        self.assertTrue(ok)
        self.assertFalse(self.mock._is_playing)

    def test_invalid_arguments(self):
        """Must specify song row + track, or chain."""
        with self.assertRaises(NavigationError):
            run_target_capture(
                dry_run=True,
                driver_instance=self.mock,
            )


if __name__ == "__main__":
    unittest.main()
