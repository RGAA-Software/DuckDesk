#!/bin/sh
set -eu

install -d -o postgres -g postgres -m 0750 \
    /var/lib/pgbackrest \
    /var/log/pgbackrest \
    /var/spool/pgbackrest

exec docker-entrypoint.sh "$@"
