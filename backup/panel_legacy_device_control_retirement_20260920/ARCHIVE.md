# Panel legacy device-control retirement

- Original paths are preserved below this directory.
- Base revision: `55d2809e6`.
- Local modification status at archival: none for the archived files.
- Retirement reason: the Panel must not self-register or mutate host devices through the retired appkey `/api/v1` API, and it must not maintain the retired `/console/panel` or `/cms/panel` presence connection. Console node authentication and the Service node-control connection are the authoritative host identity and presence path.
- Archived code is reference-only and is excluded from build discovery, packaging, tests, and runtime loading.

The archive also preserves full pre-change copies of maintained files whose legacy branches are removed in this batch. It is not a compatibility implementation.
