FROM ubuntu:24.04

ARG RELAY_VERSION
ARG RELAY_SHA256
LABEL org.opencontainers.image.title="Pixels Relay" \
      org.opencontainers.image.version="${RELAY_VERSION}" \
      io.pixels.product="relay" \
      io.pixels.runtime.sha256="${RELAY_SHA256}"
COPY --chmod=755 px_relay /opt/pixels/bin/px_relay
USER 10001:10001
WORKDIR /opt/pixels
ENTRYPOINT ["/opt/pixels/bin/px_relay"]
CMD ["--iroh-config", "/etc/pixels/relay/relay.json"]
