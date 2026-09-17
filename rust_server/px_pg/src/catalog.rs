// Composition for the standalone schema tool and its acceptance harness only.
// The shared library accepts a Migrator from its caller and embeds no domain SQL.
use px_pg::Service;
use sqlx::migrate::Migrator;

static CONSOLE: Migrator = sqlx::migrate!("../px_console_server/migrations");
static AUTH: Migrator = sqlx::migrate!("../px_auth_server/migrations");
static DESK: Migrator = sqlx::migrate!("../px_desk_server/migrations");

pub fn migrations(service: Service) -> &'static Migrator {
    match service {
        Service::Console => &CONSOLE,
        Service::Auth => &AUTH,
        Service::Desk => &DESK,
    }
}
