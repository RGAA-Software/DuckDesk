"""Run as root on CN with an already uploaded, hashed payload directory."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import sys
import tarfile
import time
import tomllib
import urllib.request


def run(args, timeout=30):
    result = subprocess.run(args, capture_output=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError('Command failed: ' + args[0])
    return result.stdout.decode('utf-8', errors='replace')


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


stage = Path(sys.argv[1]).resolve()
if stage.parent != Path('/tmp') or not stage.name.startswith('px-auth-cn-'):
    raise SystemExit('Invalid staging directory')
old = Path('/opt/gr_auth_server')
new = Path('/opt/px_auth_server')
old_conf = Path('/etc/supervisor/conf.d/gr_auth_server.conf')
new_conf = Path('/etc/supervisor/conf.d/px_auth_server.conf')
if new.exists() or new_conf.exists():
    raise SystemExit('New deployment path already exists; refusing overwrite')
if not old_conf.is_file():
    raise SystemExit('Expected legacy supervisor configuration missing')
settings = tomllib.loads((old / 'gr_auth_server_settings.toml').read_text())
if settings['server_port'] != 30400 or settings['verify_server'].rstrip('/') != 'https://auth.rgaa.vip':
    raise SystemExit('Unexpected production endpoint')
if not (old / 'certs/auth_license_private.key').is_file():
    raise SystemExit('Existing signing key is required; refusing implicit key generation')
manifest = json.loads((stage / 'manifest.json').read_text())
for relative, digest in manifest.items():
    candidate = (stage / relative).resolve()
    if not candidate.is_relative_to(stage) or sha(candidate) != digest:
        raise SystemExit('Payload hash mismatch')
if 'not found' in run(['ldd', str(stage / 'px_auth')]):
    raise SystemExit('Missing runtime dependency')
stamp = time.strftime('%Y%m%d-%H%M%S')
backup = Path('/opt/px_auth_backups') / stamp
backup.mkdir(parents=True, mode=0o700)
os.chmod(backup.parent, 0o700)
os.chmod(backup, 0o700)
with tarfile.open(backup / 'legacy-deployment.tar.gz', 'w:gz') as archive:
    for entry in old.iterdir():
        if entry.name != 'logs':
            archive.add(entry, arcname='gr_auth_server/' + entry.name)
shutil.copy2(old_conf, backup / old_conf.name)
shutil.copytree('/etc/nginx', backup / 'nginx', symlinks=True)
new.mkdir(mode=0o755)
shutil.copy2(stage / 'px_auth', new / 'px_auth')
os.chmod(new / 'px_auth', 0o755)
shutil.copytree(stage / 'web_auth', new / 'web_auth')
shutil.copytree(old / 'certs', new / 'certs')
shutil.copy2(old / 'gr_auth_server_settings.toml', new / 'px_auth.toml')
os.chmod(new / 'px_auth.toml', 0o600)
os.chmod(new / 'certs/auth_license_private.key', 0o600)
(new / 'logs').mkdir()
conf = old_conf.read_text().replace('[program:gr_auth_server]', '[program:px_auth_server]')
conf = conf.replace('/opt/gr_auth_server/gr_auth_server', '/opt/px_auth_server/px_auth')
conf = conf.replace('/opt/gr_auth_server', '/opt/px_auth_server')
run(['chown', '-R', 'ubuntu:ubuntu', str(new)])
stopped = False
disabled_conf = old_conf.with_suffix('.conf.disabled-' + stamp)
try:
    run(['supervisorctl', 'stop', 'gr_auth_server'])
    stopped = True
    run(['mongodump', '--uri', settings['db_path'], '--db', 'db_gr_auth_server',
         '--archive=' + str(backup / 'database.archive.gz'), '--gzip'], timeout=90)
    os.chmod(backup / 'database.archive.gz', 0o600)
    print('Backup complete:', backup, flush=True)
    old_conf.rename(disabled_conf)
    new_conf.write_text(conf)
    os.chmod(new_conf, 0o600)
    run(['supervisorctl', 'reread'])
    run(['supervisorctl', 'update'])
    context = ssl._create_unverified_context()  # Loopback only; public URL below verifies TLS.
    for attempt in range(20):
        try:
            with urllib.request.urlopen('https://127.0.0.1:30400/api/v1/ping', context=context, timeout=2) as response:
                if response.status == 200:
                    break
        except Exception:
            pass
        time.sleep(0.5)
    else:
        raise RuntimeError('New service readiness failed')
    for relative in ['/', '/api/v1/ping']:
        with urllib.request.urlopen('https://auth.rgaa.vip' + relative, timeout=10) as response:
            if response.status != 200:
                raise RuntimeError('Public HTTPS validation failed')
    for relative in ['certs/auth_license_private.key', 'certs/auth_license_public.key']:
        if sha(old / relative) != sha(new / relative):
            raise RuntimeError('Signing key changed unexpectedly')
    for relative, digest in manifest.items():
        if sha(new / relative) != digest:
            raise RuntimeError('Installed artifact mismatch')
    print(run(['supervisorctl', 'status']), flush=True)
    print('Public HTTPS and ping: 200; keys preserved; all artifact hashes match', flush=True)
    print('Binary SHA256:', sha(new / 'px_auth'), flush=True)
except Exception as error:
    if stopped:
        subprocess.run(['supervisorctl', 'stop', 'px_auth_server'], capture_output=True, timeout=30)
        if new_conf.exists():
            new_conf.rename(backup / 'failed-px_auth_server.conf')
        if disabled_conf.exists():
            disabled_conf.rename(old_conf)
        run(['supervisorctl', 'reread'])
        run(['supervisorctl', 'update'])
        subprocess.run(['supervisorctl', 'start', 'gr_auth_server'], capture_output=True, timeout=30)
        print('Rolled back to legacy supervisor service; retained failed release and backup', flush=True)
    raise SystemExit(type(error).__name__ + ': ' + str(error))
