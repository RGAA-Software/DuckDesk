# Panel endpoint model retirement

- Original paths are preserved below this directory.
- Base revision: `a1be6c6f3`.
- Local modification status at archival: none for the archived files.
- Retirement reason: Panel must accept a current Console HTTPS endpoint rather than the retired encrypted authorization payload, and deployment Relay credentials must travel only over the authenticated Console-to-Service node-control connection.
- The archived shared-link Relay fields, Panel-to-Render Relay synchronization, and obsolete access-information localization exposed or described the retired deployment credential flow and are retired with the same batch.
- Archived code is reference-only and is excluded from build discovery, packaging, tests, and runtime loading.

The archive preserves complete pre-change files, including maintained files from which retired branches are removed. It is not a compatibility implementation.
