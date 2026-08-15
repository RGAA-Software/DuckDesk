# px_auth_server Runtime Config

`px_auth_server` does not embed default passwords or JWT secrets in source code. Runtime security-sensitive values are read from `gr_auth_server_settings.toml`.

Required config:

```toml
[bootstrap]
jwt_secret = "random-secret-with-at-least-32-characters"
admin_name = "Admin"
admin_password = "initial-admin-password"
visitor_name = "Visitor"
visitor_password = ""
```

Notes:

- `bootstrap.jwt_secret` is required on every startup. Changing it invalidates existing JWT login tokens.
- `bootstrap.admin_password` is required only when no admin account exists in MongoDB.
- `bootstrap.visitor_password` is optional. Leave it empty to skip creating the visitor account.
- Placeholder values such as `CHANGE_ME_ADMIN_PASSWORD` are rejected and will not create an account.
