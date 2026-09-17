CREATE TABLE pixels.control_runtime (
 singleton BOOLEAN PRIMARY KEY DEFAULT true CHECK (singleton),
 epoch BIGINT NOT NULL DEFAULT 0 CHECK (epoch>=0)
);
INSERT INTO pixels.control_runtime(singleton) VALUES(true);
CREATE TABLE pixels.control_runs (
 epoch BIGINT PRIMARY KEY CHECK (epoch>0),
 instance_id UUID NOT NULL UNIQUE,
 started_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp()
);
CREATE TABLE pixels.nodes (
 id UUID PRIMARY KEY,
 device_id UUID NOT NULL UNIQUE REFERENCES pixels.devices(id),
 product TEXT NOT NULL CHECK (product IN ('cloud_node','remote')),
 credential_hash BYTEA NOT NULL UNIQUE CHECK (octet_length(credential_hash)=32),
 revision BIGINT NOT NULL DEFAULT 1 CHECK (revision>0),
 generation BIGINT NOT NULL DEFAULT 1 CHECK (generation>0),
 control_epoch BIGINT REFERENCES pixels.control_runs(epoch),
 connection_hash BYTEA UNIQUE CHECK (octet_length(connection_hash)=32),
 state TEXT NOT NULL DEFAULT 'offline' CHECK (state IN ('offline','reconciling','ready')),
 draining BOOLEAN NOT NULL DEFAULT false,
 disabled BOOLEAN NOT NULL DEFAULT false,
 max_instances INTEGER NOT NULL CHECK (max_instances>=1 AND max_instances<=64),
 report_sequence BIGINT NOT NULL DEFAULT 0 CHECK (report_sequence>=0),
 reconciliation_id UUID,
 reconciliation_deadline TIMESTAMPTZ,
 last_seen TIMESTAMPTZ,
 product_version_code BIGINT CHECK (product_version_code>0 AND product_version_code<=4294967295),
 public_host TEXT CHECK (char_length(public_host)>=1 AND char_length(public_host)<=253),
 desktop_port INTEGER CHECK (desktop_port>=1 AND desktop_port<=65535),
 application_port_start INTEGER CHECK (application_port_start>=1 AND application_port_start<=65535),
 application_port_end INTEGER CHECK (application_port_end>=1 AND application_port_end<=65535),
 game_hook BOOLEAN NOT NULL DEFAULT false,
 webview BOOLEAN NOT NULL DEFAULT false,
 rdp BOOLEAN NOT NULL DEFAULT false,
 endpoint_revision BIGINT NOT NULL DEFAULT 1 CHECK (endpoint_revision>0),
 registered_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 deleted_at TIMESTAMPTZ,
 CHECK ((connection_hash IS NULL)=(state='offline')),
 CHECK (connection_hash IS NULL OR control_epoch IS NOT NULL),
 CHECK (connection_hash IS NULL OR last_seen IS NOT NULL),
 CHECK ((reconciliation_id IS NULL)=(reconciliation_deadline IS NULL)),
 CHECK (connection_hash IS NOT NULL OR reconciliation_id IS NULL),
 CHECK ((public_host IS NULL)=(desktop_port IS NULL)),
 CHECK ((application_port_start IS NULL)=(application_port_end IS NULL)),
 CHECK (application_port_start<=application_port_end),
 CHECK (desktop_port<application_port_start OR desktop_port>application_port_end),
 CHECK (NOT disabled OR state='offline'),
 CHECK (deleted_at IS NULL OR state='offline')
);
CREATE TABLE pixels.node_audit (
 id UUID PRIMARY KEY,
 node_id UUID NOT NULL REFERENCES pixels.nodes(id),
 actor_id UUID NOT NULL REFERENCES pixels.users(id),
 revision BIGINT NOT NULL CHECK (revision>0),
 action TEXT NOT NULL CHECK (action IN ('created','configured','key_rotated','deleted')),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(node_id,revision)
);
GRANT SELECT ON pixels.control_runtime TO pixels_console_runtime;
GRANT UPDATE(epoch) ON pixels.control_runtime TO pixels_console_runtime;
GRANT SELECT,INSERT ON pixels.control_runs,pixels.nodes,pixels.node_audit TO pixels_console_runtime;
GRANT UPDATE(credential_hash,revision,generation,control_epoch,connection_hash,state,draining,disabled,max_instances,report_sequence,last_seen,
 product_version_code,public_host,desktop_port,application_port_start,application_port_end,game_hook,webview,rdp,endpoint_revision,deleted_at,reconciliation_id,reconciliation_deadline)
 ON pixels.nodes TO pixels_console_runtime;
