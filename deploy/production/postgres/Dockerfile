FROM postgres:18.6@sha256:4ef4dbc939d61acea57712655ddb4b4ab27419c913f94cca0cd57cb3ea3c2280

ARG PGBACKREST_VERSION=2.59.1-1.pgdg13+1

RUN apt-get update \
    && apt-get install --yes --no-install-recommends "pgbackrest=${PGBACKREST_VERSION}" \
    && pgbackrest version \
    && rm -rf /var/lib/apt/lists/*

COPY pixels-entrypoint.sh /usr/local/bin/pixels-postgres-entrypoint
RUN chmod 0755 /usr/local/bin/pixels-postgres-entrypoint

LABEL org.opencontainers.image.vendor="Pixels" \
      org.opencontainers.image.title="Pixels PostgreSQL with pgBackRest"

ENTRYPOINT ["pixels-postgres-entrypoint"]
CMD ["postgres"]
