# PostgreSQL Android API retirement archive

- Base revision: `b7bdf60c0`
- Local modification status: both archived scripts were unchanged from the base revision.
- Original paths:
  - `scripts/test_android_cloud_apps_public.py`
  - `scripts/test_android_registration_public.py`
- Retirement reason: the scripts used the deleted `/api/v1` guest, account, application, instance and password-hash connection contracts.
- Active replacement: scripts at the original paths use `/api/console`, `client_type=android`, explicit subject kind and typed resource-session descriptors.
- Archive policy: reference only; excluded from build, tests, packaging and runtime loading.
