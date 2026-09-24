#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 || "$1" != /* || ( $# -eq 2 && "$2" != --retain-for-systemd )
      || ( $# -eq 2 && "${PIXELS_PG_INTEGRATED_TEST:-}" != 1 )
      || "${PIXELS_PG_ISOLATED_TEST:-}" != 1
      || ! "${PIXELS_TEST_CONTAINER:-}" =~ ^[a-f0-9]{12,64}$
      || ! "${PIXELS_DEPLOYMENT_ID:-}" =~ ^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$
      || -z "${PIXELS_TEST_CONSOLE_OWNER_URL:-}" || -z "${PIXELS_TEST_CONSOLE_RUNTIME_URL:-}"
      || -z "${PIXELS_TEST_DESK_OWNER_URL:-}" || -z "${PIXELS_TEST_DESK_RUNTIME_URL:-}" ]]; then
    echo "usage (with isolated PostgreSQL): private_candidate_bootstrap.sh <absolute-candidate-directory> [--retain-for-systemd]" >&2
    exit 2
fi
candidate_directory=$1
python3 "$candidate_directory/tools/verify_candidate.py" "$candidate_directory"
[[ -x "$candidate_directory/bin/px_db" ]] || { echo "packaged database administrator is unavailable" >&2; exit 2; }

database_suffix=${PIXELS_DEPLOYMENT_ID:0:8}
console_database="pixels_console_candidate_$database_suffix"
desk_database="pixels_desk_candidate_$database_suffix"
retain_for_systemd=${2:-}
bootstrap_succeeded=0
created_databases=()
cleanup() {
    cleanup_status=$?
    set +e
    if [[ "$retain_for_systemd" != --retain-for-systemd || "$bootstrap_succeeded" != 1 ]]; then
        for database_name in "${created_databases[@]}"; do
            docker exec "$PIXELS_TEST_CONTAINER" dropdb --if-exists --force -U pixels_admin "$database_name" >/dev/null 2>&1
        done
    fi
    trap - EXIT
    exit "$cleanup_status"
}
trap cleanup EXIT

for service_name in console desk; do
    database_name="pixels_${service_name}_candidate_$database_suffix"
    owner_role="pixels_${service_name}_owner"
    runtime_role="pixels_${service_name}_runtime"
    docker exec "$PIXELS_TEST_CONTAINER" createdb -U pixels_admin -O "$owner_role" "$database_name"
    created_databases+=("$database_name")
    docker exec "$PIXELS_TEST_CONTAINER" psql -X -v ON_ERROR_STOP=1 -U pixels_admin -d postgres -c \
        "REVOKE ALL ON DATABASE $database_name FROM PUBLIC; GRANT CONNECT ON DATABASE $database_name TO $runtime_role" >/dev/null
    docker exec "$PIXELS_TEST_CONTAINER" psql -X -v ON_ERROR_STOP=1 -U pixels_admin -d "$database_name" -c \
        "REVOKE ALL ON SCHEMA public FROM PUBLIC; SET ROLE $owner_role; CREATE SCHEMA pixels; \
        CREATE TABLE pixels.deployment_identity (singleton boolean PRIMARY KEY DEFAULT true CHECK(singleton), \
        deployment_id uuid NOT NULL, service text NOT NULL CHECK(service IN ('console', 'auth', 'desk'))); \
        INSERT INTO pixels.deployment_identity(deployment_id, service) VALUES ('$PIXELS_DEPLOYMENT_ID'::uuid, '$service_name'); \
        GRANT USAGE ON SCHEMA pixels TO $runtime_role; GRANT SELECT ON pixels.deployment_identity TO $runtime_role" >/dev/null

    owner_variable="PIXELS_TEST_${service_name^^}_OWNER_URL"
    runtime_variable="PIXELS_TEST_${service_name^^}_RUNTIME_URL"
    owner_source_url=${!owner_variable}
    runtime_source_url=${!runtime_variable}
    [[ "$owner_source_url" == */"pixels_$service_name" && "$runtime_source_url" == */"pixels_$service_name" ]] || {
        echo "isolated $service_name DSN did not target its expected database" >&2; exit 1;
    }
    owner_database_url="${owner_source_url%/pixels_$service_name}/$database_name"
    runtime_database_url="${runtime_source_url%/pixels_$service_name}/$database_name"

    if PIXELS_DATABASE_URL="$runtime_database_url" PIXELS_PG_LOCAL_DEVELOPMENT=1 \
        "$candidate_directory/bin/px_db" check "$service_name" >/dev/null 2>&1; then
        echo "packaged $service_name schema check accepted an unmigrated database" >&2
        exit 1
    fi
    for migration_attempt in first repeat; do
        PIXELS_DATABASE_URL="$owner_database_url" PIXELS_PG_LOCAL_DEVELOPMENT=1 \
            "$candidate_directory/bin/px_db" migrate "$service_name" >/dev/null
    done
    PIXELS_DATABASE_URL="$runtime_database_url" PIXELS_PG_LOCAL_DEVELOPMENT=1 \
        "$candidate_directory/bin/px_db" check "$service_name" >/dev/null
    migration_count=$(docker exec "$PIXELS_TEST_CONTAINER" psql -X -At -U pixels_admin -d "$database_name" -c \
        'SELECT count(*) FROM pixels._sqlx_migrations WHERE success')
    [[ "$migration_count" =~ ^[1-9][0-9]*$ ]] || { echo "packaged $service_name migrations were not recorded" >&2; exit 1; }
    if PIXELS_DEPLOYMENT_ID=00000000-0000-4000-8000-000000000000 \
        PIXELS_DATABASE_URL="$runtime_database_url" PIXELS_PG_LOCAL_DEVELOPMENT=1 \
        "$candidate_directory/bin/px_db" check "$service_name" >/dev/null 2>&1; then
        echo "packaged $service_name accepted another deployment identity" >&2
        exit 1
    fi
done
bootstrap_succeeded=1
echo "PASS PRIVATE CANDIDATE BOOTSTRAP: packaged px_db initialized fresh Console and Desk schemas, repeated safely and rejected wrong deployment"
