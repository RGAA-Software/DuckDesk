# Pixels backup monitoring

`px_backup` atomically publishes `metrics.prom` beside `status.json`. Configure the host's trusted Prometheus textfile collector to read
that deployment-specific status directory, then load `pixels-backup.rules.yml` into Prometheus. The exported labels contain only the
deployment UUID; recovery-set IDs, paths, credentials and failure details are intentionally omitted.

The textfile directory must remain writable only by the deployment backup service and readable by the collector. Do not make the backup
repository or private configuration readable by the collector. Configure Alertmanager routing and receivers outside this repository so
customer addresses and credentials stay in the deployment secret store.

The rules detect a missing metric family, stale service heartbeat, repeated task failures, an overdue verified backup and an unhealthy
configured offsite repository. A successful `promtool check rules` proves syntax only; production acceptance also requires an actual test
alert to traverse Prometheus, Alertmanager and the configured receiver.
