# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the authenticated helper without elevating privileges or using APT."""
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

GENERATED = Path(sys.argv.pop(1))
SOURCE = Path(__file__).resolve().parents[1] / 'src'
spec = importlib.util.spec_from_file_location('installer', SOURCE / 'installer.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class BoundaryTests(unittest.TestCase):
    def test_policy_matches_configured_helper(self):
        paths = {}
        exec((GENERATED / 'paths.py').read_text(), paths)
        action = ET.parse(GENERATED / 'org.lumen.DebInstall.policy').find('action')
        self.assertEqual(action.findtext('defaults/allow_any'), 'no')
        self.assertEqual(action.findtext('defaults/allow_inactive'), 'no')
        self.assertEqual(action.findtext('defaults/allow_active'), 'auth_admin')
        self.assertEqual(action.findtext("annotate[@key='org.freedesktop.policykit.exec.path']"), paths['HELPER'])

    def test_missing_requesting_user(self):
        with patch.object(os, 'geteuid', return_value=0), \
                patch.dict(os.environ, {}, clear=True), \
                patch.object(sys, 'argv', ['helper', 'snapshot', '0' * 64, '0' * 64]):
            with self.assertRaises(PermissionError):
                installer.main()

    def test_changed_plan_and_environment(self):
        result = {'ready': True, 'plan_token': '1' * 64}
        from types import SimpleNamespace
        from unittest.mock import Mock
        application = Mock()
        inspect = Mock(return_value=(result, application))
        with patch.object(os, 'geteuid', return_value=0), \
                patch.dict(os.environ, {'PKEXEC_UID': '1000', 'APT_CONFIG': '/untrusted'}, clear=True), \
                patch.object(sys, 'argv', ['helper', 'snapshot', '0' * 64, '0' * 64]), \
                patch.object(installer, 'copy_verified'), \
                patch.dict(sys.modules, {'backend': SimpleNamespace(inspect_package=inspect)}):
            with self.assertRaisesRegex(ValueError, 'state or dependencies changed'):
                installer.main()
            self.assertNotIn('APT_CONFIG', os.environ)
            self.assertEqual(os.environ['HOME'], '/root')
            inspect.assert_called_once()
            application.install.assert_not_called()

    def test_fifo_rejected_without_blocking(self):
        with tempfile.TemporaryDirectory() as directory:
            fifo = Path(directory) / 'fifo'
            os.mkfifo(fifo)
            with self.assertRaises(ValueError):
                installer.copy_verified(fifo, Path(directory) / 'copy', os.getuid(), '0' * 64)

    def test_privileged_copy_deadline(self):
        with patch.object(installer, 'copy_verified', side_effect=lambda *_: time.sleep(1)):
            started = time.monotonic()
            with self.assertRaisesRegex(TimeoutError, '120-second deadline'):
                installer.copy_with_deadline('source', 'target', os.getuid(), '0' * 64, timeout=0.05)
            self.assertLess(time.monotonic() - started, 0.5)


if __name__ == '__main__':
    unittest.main()
