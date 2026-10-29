# Copyright 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
"""Run the installer fixture on the shared unmapped/private X test display."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
if len(sys.argv) > 1 and sys.argv[1] == '--inner':
    wm = subprocess.Popen(['openbox'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        raise SystemExit(subprocess.call(['/usr/bin/python3', str(root/'tests/ui_smoke.py'), *sys.argv[2:]]))
    finally:
        wm.terminate()
        wm.wait(timeout=5)

with tempfile.TemporaryDirectory(prefix='debinstall-ui-') as directory:
    work = Path(directory)
    control = work/'fixture/DEBIAN'
    control.mkdir(parents=True, mode=0o755)
    control.chmod(0o755)
    (control/'control').write_text('Package: debinstall-test-fixture\nVersion: 1.0\nArchitecture: all\nMaintainer: Test <test@example.invalid>\nDescription: UI smoke fixture\n')
    package = work/'fixture.deb'
    subprocess.run(['dpkg-deb','--build',str(control.parent),str(package)],check=True)
    env = os.environ.copy()
    env['LUXE_TEST_SCREEN'] = '1000x800'
    result = subprocess.run([str(root.parent/'cmake/RunNestedX.sh'),
        '/usr/bin/python3', str(Path(__file__).resolve()), '--inner', str(package), str(work/'package.png')], env=env)
    raise SystemExit(result.returncode)
