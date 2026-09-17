#!/usr/bin/env bash
set -Eeuo pipefail

# Only runs on a new development volume. Secrets are read through the environment,
# never passed as command arguments or printed. Identifiers are a closed list.
for service in console auth desk; do
  export PIXELS_SERVICE="$service"
  upper="${service^^}"
  owner_key="${upper}_OWNER_PASSWORD"
  runtime_key="${upper}_RUNTIME_PASSWORD"
  export PIXELS_OWNER_PASSWORD="${!owner_key}"
  export PIXELS_RUNTIME_PASSWORD="${!runtime_key}"
  export PIXELS_DB="pixels_${service}"
  export PIXELS_OWNER="pixels_${service}_owner"
  export PIXELS_RUNTIME="pixels_${service}_runtime"
  psql -X -v ON_ERROR_STOP=1 --username pixels_admin --dbname postgres <<'SQL'
\getenv db PIXELS_DB
\getenv owner PIXELS_OWNER
\getenv runtime PIXELS_RUNTIME
\getenv owner_password PIXELS_OWNER_PASSWORD
\getenv runtime_password PIXELS_RUNTIME_PASSWORD
SELECT format('CREATE ROLE %I LOGIN PASSWORD %L NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION', :'owner', :'owner_password') \gexec
SELECT format('CREATE ROLE %I LOGIN PASSWORD %L NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION', :'runtime', :'runtime_password') \gexec
SELECT format('CREATE DATABASE %I OWNER %I', :'db', :'owner') \gexec
SELECT format('REVOKE ALL ON DATABASE %I FROM PUBLIC', :'db') \gexec
SELECT format('GRANT CONNECT ON DATABASE %I TO %I', :'db', :'runtime') \gexec
SQL
  psql -X -v ON_ERROR_STOP=1 --username pixels_admin --dbname "$PIXELS_DB" <<'SQL'
\getenv owner PIXELS_OWNER
\getenv runtime PIXELS_RUNTIME
\getenv service PIXELS_SERVICE
\getenv deployment PIXELS_DEPLOYMENT_ID
REVOKE ALL ON SCHEMA public FROM PUBLIC;
SET ROLE :"owner";
CREATE SCHEMA pixels;
CREATE TABLE pixels.deployment_identity (
  singleton BOOLEAN PRIMARY KEY DEFAULT TRUE CHECK(singleton),
  deployment_id UUID NOT NULL,
  service TEXT NOT NULL CHECK(service IN ('console', 'auth', 'desk'))
);
INSERT INTO pixels.deployment_identity(deployment_id, service) VALUES (:'deployment'::uuid, :'service');
GRANT USAGE ON SCHEMA pixels TO :"runtime";
GRANT SELECT ON pixels.deployment_identity TO :"runtime";
SQL
done
psql -X -v ON_ERROR_STOP=1 --username pixels_admin --dbname postgres <<'SQL'
REVOKE ALL ON DATABASE postgres FROM PUBLIC;
REVOKE ALL ON DATABASE template1 FROM PUBLIC;
SQL
