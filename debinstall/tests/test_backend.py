# Copyright 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
import hashlib,importlib.util,os,sys,tempfile,unittest,subprocess
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'src'))
import backend
from snapshot_copy import copy_snapshot
import snapshot
import threading
spec=importlib.util.spec_from_file_location('installer',ROOT/'src/installer.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)
class Tests(unittest.TestCase):
    def setUp(self):self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
    def tearDown(self):self.temp.cleanup()
    def package(self,name='debinstall-test-fixture',depends='',architecture='all'):
        folder=self.root/name;(folder/'DEBIAN').mkdir(parents=True)
        # dpkg-deb rejects private directory modes; test runners may inherit a
        # restrictive umask from the desktop session.
        (folder/'DEBIAN').chmod(0o755)
        (folder/'DEBIAN/control').write_text(f'Package: {name}\nVersion: 1.0\nArchitecture: {architecture}\nMaintainer: Test <test@example.invalid>\nDescription: Test package\n'+(f'Depends: {depends}\n' if depends else ''))
        payload=folder/'usr/share/debinstall-test';payload.mkdir(parents=True)
        (payload/'marker').write_text('fixture payload')
        out=self.root/(name+'.deb');subprocess.run(['dpkg-deb','--build',str(folder),str(out)],check=True,stdout=subprocess.DEVNULL);return out
    def test_valid_plan(self):
        path=self.package();data,_=backend.inspect_package(path)
        self.assertTrue(data['ready']);self.assertEqual(data['changes'],[])
        self.assertEqual(data['package'],'debinstall-test-fixture')
        self.assertIn('./usr/share/debinstall-test/marker', data['files'])
        again,_=backend.inspect_package(path);self.assertEqual(data['plan_token'],again['plan_token'])
    def test_missing_dependency(self):
        data,_=backend.inspect_package(self.package(depends='debinstall-nonexistent-dependency-78462'))
        self.assertFalse(data['ready']);self.assertTrue(data['failure'])
    def test_wrong_architecture(self):
        data,_=backend.inspect_package(self.package(architecture='debinstall-invalid-arch'))
        self.assertFalse(data['ready'])
    def test_invalid_archive(self):
        p=self.root/'bad.deb';p.write_text('not a deb')
        with self.assertRaises(Exception):backend.inspect_package(p)
    def test_verified_snapshot(self):
        source=self.root/'source';source.write_bytes(b'package');target=self.root/'copy'
        digest=hashlib.sha256(b'package').hexdigest()
        installer.copy_verified(source,target,os.getuid(),digest);self.assertEqual(target.read_bytes(),b'package')

    def test_gui_snapshot_copy_worker(self):
        source=self.root/'source';source.write_bytes(b'package')
        target=self.root/'worker-copy'
        digest=copy_snapshot(source,target,threading.Event())
        self.assertEqual(target.read_bytes(),b'package')
        self.assertEqual(digest,hashlib.sha256(b'package').hexdigest())

    def test_gdebi_private_api_adapter(self):
        from types import SimpleNamespace
        application=SimpleNamespace(_cache=object(),_deb=None,install=lambda:0)
        package=SimpleNamespace(_sections={'Package':'fixture'},_failure_string='broken')
        adapter=backend.GDebiAdapter(application)
        adapter.bind_package(package)
        self.assertIs(application._deb,package)
        self.assertIs(adapter.cache,application._cache)
        self.assertEqual(adapter.fields(package),{'Package':'fixture'})
        self.assertEqual(adapter.failure(package),'broken')
        self.assertEqual(adapter.install(),0)

    def test_package_file_listing_is_capped(self):
        lines = [
            f'-rw-r--r-- root/root 1 2026-10-28 00:00 ./file-{index}'
            for index in range(backend.MAX_FILES + 3)
        ]
        result = subprocess.CompletedProcess([], 0, '\n'.join(lines), '')
        with patch.object(backend, 'run_bounded', return_value=result) as run:
            files, hidden = backend.list_package_files(self.root / 'fixture.deb')
        self.assertEqual(len(files), backend.MAX_FILES)
        self.assertEqual(hidden, 3)
        self.assertEqual(files[0], './file-0')
        run.assert_called_once()
    def test_changed_snapshot(self):
        source=self.root/'source';source.write_bytes(b'changed');target=self.root/'copy'
        with self.assertRaises(ValueError):installer.copy_verified(source,target,os.getuid(),'0'*64)
        self.assertFalse(target.exists())
    def test_foreign_owner(self):
        source=self.root/'source';source.write_bytes(b'package')
        with self.assertRaises(ValueError):installer.copy_verified(source,self.root/'copy',os.getuid()+1,'0'*64)
    def test_symlink_snapshot(self):
        source=self.root/'source';source.write_bytes(b'package');link=self.root/'link';link.symlink_to(source)
        with self.assertRaises(OSError):installer.copy_verified(link,self.root/'copy',os.getuid(),'0'*64)

    def test_oversized_sparse_snapshot_is_rejected_before_copy(self):
        source = self.root / 'oversized.deb'
        with source.open('wb') as stream:
            stream.truncate(snapshot.MAX_PACKAGE_BYTES + 1)
        target = self.root / 'oversized-copy'
        with self.assertRaisesRegex(ValueError, '2 GiB inspection limit'):
            snapshot.copy_package(source, target)
        self.assertFalse(target.exists())

    def test_snapshot_copy_detects_source_growth(self):
        source = self.root / 'growing.deb'
        source.write_bytes(b'initial bytes')
        target = self.root / 'growing-copy'
        real_fdopen = snapshot.os.fdopen

        class AppendAfterRead:
            def __init__(self, wrapped):
                self.wrapped = wrapped
                self.appended = False

            def __enter__(self):
                self.wrapped.__enter__()
                return self

            def __exit__(self, *args):
                return self.wrapped.__exit__(*args)

            def __getattr__(self, name):
                return getattr(self.wrapped, name)

            def read(self, size=-1):
                block = self.wrapped.read(size)
                if not self.appended:
                    with source.open('ab') as stream:
                        stream.write(b'growth')
                    self.appended = True
                return block

        with patch.object(snapshot.os, 'fdopen', side_effect=lambda *args, **kwargs:
                          AppendAfterRead(real_fdopen(*args, **kwargs))):
            with self.assertRaisesRegex(ValueError, 'grew'):
                snapshot.copy_package(source, target)
        self.assertFalse(target.exists())
    def test_helper_requires_authentication(self):
        if os.geteuid()==0:self.skipTest('non-root test')
        result=subprocess.run(['/usr/bin/python3','-I',str(ROOT/'src/installer.py')],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0);self.assertIn('authenticate',result.stderr)
if __name__=='__main__':unittest.main()
