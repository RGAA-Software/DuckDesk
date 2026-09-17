CREATE TABLE pixels.rdp_workspaces (
 id UUID PRIMARY KEY,
 application_id UUID NOT NULL,
 deployment_id UUID NOT NULL,
 node_id UUID NOT NULL,
 kind TEXT NOT NULL DEFAULT 'rdp' CHECK (kind='rdp'),
 account_name TEXT NOT NULL CHECK (account_name ~ '^pxrdp_[a-f0-9]{14}$'),
 windows_sid TEXT CHECK (windows_sid ~ '^S-1-5-21-[0-9]{1,10}-[0-9]{1,10}-[0-9]{1,10}-[0-9]{1,10}$'),
 state TEXT NOT NULL DEFAULT 'provisioning' CHECK (state IN ('provisioning','ready')),
 revision BIGINT NOT NULL DEFAULT 1 CHECK (revision>0),
 credential_revision BIGINT NOT NULL DEFAULT 1 CHECK (credential_revision>0),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 FOREIGN KEY(deployment_id,application_id,node_id,kind) REFERENCES pixels.application_deployments(id,application_id,node_id,kind),
 UNIQUE(application_id,node_id),
 UNIQUE(node_id,account_name),
 UNIQUE(node_id,windows_sid),
 CHECK ((state='ready')=(windows_sid IS NOT NULL))
);
CREATE TABLE pixels.workspace_secrets (
 workspace_id UUID PRIMARY KEY REFERENCES pixels.rdp_workspaces(id),
 schema_version INTEGER NOT NULL CHECK (schema_version=1),
 key_id UUID NOT NULL,
 nonce BYTEA NOT NULL CHECK (octet_length(nonce)=12),
 ciphertext BYTEA NOT NULL CHECK (octet_length(ciphertext)=84),
 UNIQUE(key_id,nonce)
);
CREATE TABLE pixels.workspace_audit (
 id UUID PRIMARY KEY,
 workspace_id UUID NOT NULL REFERENCES pixels.rdp_workspaces(id),
 revision BIGINT NOT NULL CHECK (revision>0),
 actor_id UUID REFERENCES pixels.users(id),
 node_id UUID REFERENCES pixels.nodes(id),
 command_id UUID REFERENCES pixels.instance_commands(id),
 action TEXT NOT NULL CHECK (action IN ('created','confirmed','rewrapped')),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 CHECK ((actor_id IS NULL)<>(node_id IS NULL)),
 CHECK ((action='rewrapped')=(actor_id IS NOT NULL)),
 CHECK ((command_id IS NULL)=(node_id IS NULL)),
 UNIQUE(workspace_id,revision)
);
GRANT SELECT,INSERT ON pixels.rdp_workspaces,pixels.workspace_secrets,pixels.workspace_audit TO pixels_console_runtime;
GRANT UPDATE(windows_sid,state,revision,updated_at) ON pixels.rdp_workspaces TO pixels_console_runtime;
GRANT UPDATE(key_id,nonce,ciphertext) ON pixels.workspace_secrets TO pixels_console_runtime;
