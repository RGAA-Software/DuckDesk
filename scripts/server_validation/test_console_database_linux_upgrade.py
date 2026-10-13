#!/usr/bin/env python3
"""Exercise the shipped Linux schema-upgrade helper against an owned PostgreSQL container."""
import argparse
import json
import secrets
import subprocess
import uuid
from pathlib import Path
parser = argparse.ArgumentParser()
parser.add_argument('--image', required=True)
parser.add_argument('--report', type=Path, required=True)
arguments = parser.parse_args()
fixture_suffix = uuid.uuid4().hex[:12]
fixture_network = 'pixels-schema42-' + fixture_suffix
postgres_container = fixture_network + '-postgres'
configuration_volume = fixture_network + '-configuration'
state_volume = fixture_network + '-state'
postgres_password = secrets.token_hex(24)
validation_report = {'image': arguments.image, 'scope': 'real-linux-image-cli-isolated-postgresql', 'remote_linux_installation': False}
created_resources = []

def run_docker_command(command, input_text=None, allow_failure=False):
    result = subprocess.run(command, input=input_text, text=True, encoding='utf-8', capture_output=True, timeout=120)
    if result.returncode and (not allow_failure):
        arguments.report.parent.mkdir(parents=True, exist_ok=True)
        (arguments.report.parent / 'last-command.stderr.txt').write_text(result.stderr, encoding='utf-8')
        raise RuntimeError('Command failed: ' + command[0] + ' ' + command[1] + ' exit=' + str(result.returncode))
    return result

def run_server_image(command, input_text=None, allow_failure=False):
    return run_docker_command(['docker', 'run', '--rm', '-i', '--user', '0:0', '--network', fixture_network, '--mount', 'type=volume,source=' + configuration_volume + ',target=/etc/pixels', '--mount', 'type=volume,source=' + state_volume + ',target=/var/lib/pixels', arguments.image, *command], input_text, allow_failure)

def run_fixture_sql(statement):
    return run_docker_command(['docker', 'exec', '-i', postgres_container, 'psql', '--username=postgres', '--dbname=pixels_console', '--no-psqlrc', '--tuples-only', '--no-align', '--set=ON_ERROR_STOP=1'], statement).stdout.strip()
try:
    run_docker_command(['docker', 'network', 'create', '--label', 'pixels.validation=schema42', fixture_network])
    created_resources.append(('network', fixture_network))
    for volume_name in [configuration_volume, state_volume]:
        run_docker_command(['docker', 'volume', 'create', '--label', 'pixels.validation=schema42', volume_name])
        created_resources.append(('volume', volume_name))
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    environment_file = arguments.report.parent / ('postgres-' + fixture_suffix + '.env')
    environment_file.write_text('POSTGRES_PASSWORD=' + postgres_password + '\n', encoding='utf-8')
    try:
        run_docker_command(['docker', 'run', '--detach', '--name', postgres_container, '--label', 'pixels.validation=schema42', '--network', fixture_network, '--env-file', str(environment_file), 'postgres:18.6'])
    finally:
        environment_file.unlink()
    created_resources.append(('container', postgres_container))
    import time
    for readiness_attempt in range(80):
        if run_docker_command(['docker', 'exec', postgres_container, 'pg_isready', '--username=postgres'], allow_failure=True).returncode == 0:
            break
        time.sleep(0.5)
    else:
        raise RuntimeError('Owned PostgreSQL readiness timed out')
    request = {'postgresql_host': postgres_container, 'postgresql_port': 5432, 'postgresql_administrator': 'postgres', 'postgresql_password': postgres_password, 'public_host': '127.0.0.1', 'initial_username': 'Pixels', 'initial_password': secrets.token_urlsafe(32)}
    initialized = run_server_image(['/opt/pixels/bin/px_console_admin', 'initialize-single-server', '/etc/pixels', '/var/lib/pixels', '/opt/pixels', '/opt/pixels', 'linux'], json.dumps(request))
    original_backup_configuration = json.loads(run_server_image(['cat', '/etc/pixels/backup.json']).stdout)
    console_identity_hashes = run_server_image(['sha256sum', '/etc/pixels/console.env', '/etc/pixels/console-ca.crt']).stdout
    backup_ownership = run_server_image(['stat', '--format=%u:%g:%a', '/etc/pixels/backup.json']).stdout
    run_fixture_sql('BEGIN; ALTER TABLE pixels.relay_nodes DROP COLUMN bandwidth_capacity_mbps, DROP COLUMN forwarded_rate_bps; DELETE FROM pixels._sqlx_migrations WHERE version=42; COMMIT;')
    run_server_image(['bash', '-c', 'touch /etc/pixels/setup.complete; jq \'(.plan.targets[] | select(.database.service == "console") | .database.schema_version)=41\' /etc/pixels/backup.json > /etc/pixels/.fixture-backup; cat /etc/pixels/.fixture-backup > /etc/pixels/backup.json; rm /etc/pixels/.fixture-backup'])
    upgrade = run_server_image(['bash', '/opt/pixels/upgrade_console_database.sh'])
    validation_report['upgrade_output'] = upgrade.stdout
    updated_backup_configuration = json.loads(run_server_image(['cat', '/etc/pixels/backup.json']).stdout)
    if updated_backup_configuration != original_backup_configuration:
        raise RuntimeError('Backup update changed other fields')
    if run_fixture_sql('SELECT MAX(version) FROM pixels._sqlx_migrations;') != '42':
        raise RuntimeError('Migration did not reach42')
    if console_identity_hashes != run_server_image(['sha256sum', '/etc/pixels/console.env', '/etc/pixels/console-ca.crt']).stdout:
        raise RuntimeError('Console identity changed')
    if backup_ownership != run_server_image(['stat', '--format=%u:%g:%a', '/etc/pixels/backup.json']).stdout:
        raise RuntimeError('Backup ownership changed')
    validation_report.update({'from_schema': 41, 'to_schema': 42, 'backup_schema': 42, 'other_backup_fields_equal': True, 'console_config_hashes_equal': True, 'backup_ownership': backup_ownership.strip()})
    repeated = run_server_image(['bash', '/opt/pixels/upgrade_console_database.sh'])
    validation_report['repeat_output'] = repeated.stdout
    run_server_image(['bash', '-c', 'jq \'.plan.targets |= map(select(.database.service != "console"))\' /etc/pixels/backup.json > /etc/pixels/.fixture-backup; cat /etc/pixels/.fixture-backup > /etc/pixels/backup.json; rm /etc/pixels/.fixture-backup'])
    invalid_backup_hash = run_server_image(['sha256sum', '/etc/pixels/backup.json']).stdout
    rejected = run_server_image(['bash', '/opt/pixels/upgrade_console_database.sh'], allow_failure=True)
    if rejected.returncode == 0 or invalid_backup_hash != run_server_image(['sha256sum', '/etc/pixels/backup.json']).stdout:
        raise RuntimeError('Invalid plan was accepted or modified')
    validation_report['invalid_backup_rejected_before_mutation'] = True
    validation_report['pass'] = True
finally:
    for resource_kind, resource_name in reversed(created_resources):
        run_docker_command(['docker', resource_kind, 'rm', *(['--force'] if resource_kind == 'container' else []), resource_name], allow_failure=True)
    validation_report['owned_resources_removed'] = all((run_docker_command(['docker', resource_kind, 'inspect', resource_name], allow_failure=True).returncode != 0 for resource_kind, resource_name in created_resources))
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(json.dumps(validation_report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(validation_report))
