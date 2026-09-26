FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ARG PACKAGE_DIRECTORY=.
COPY ${PACKAGE_DIRECTORY}/bin/px_console /opt/pixels/bin/px_console
COPY ${PACKAGE_DIRECTORY}/bin/px_console_admin /opt/pixels/bin/px_console_admin
COPY ${PACKAGE_DIRECTORY}/bin/px_db /opt/pixels/bin/px_db
COPY ${PACKAGE_DIRECTORY}/bin/px_relay /opt/pixels/bin/px_relay
COPY ${PACKAGE_DIRECTORY}/bin/px_backup /opt/pixels/bin/px_backup
COPY ${PACKAGE_DIRECTORY}/static/console /opt/pixels/static/console
COPY ${PACKAGE_DIRECTORY}/postgresql/18 /opt/pixels/postgresql/18
COPY ${PACKAGE_DIRECTORY}/assets/license-trust.json /opt/pixels/assets/license-trust.json

ENV LD_LIBRARY_PATH=/opt/pixels/postgresql/18/lib
WORKDIR /opt/pixels
USER 10001:10001
