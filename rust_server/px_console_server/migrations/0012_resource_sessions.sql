ALTER TABLE pixels.instances ADD CONSTRAINT instances_session_target UNIQUE(id,application_id,node_id);
ALTER TABLE pixels.nodes ADD CONSTRAINT nodes_session_target UNIQUE(id,device_id);
CREATE TABLE pixels.resource_sessions (
 id UUID PRIMARY KEY,
 target_kind TEXT NOT NULL CHECK (target_kind IN ('desktop','cloud_application')),
 device_id UUID,
 application_id UUID,
 instance_id UUID,
 node_id UUID NOT NULL REFERENCES pixels.nodes(id),
 owner_user UUID REFERENCES pixels.users(id),
 owner_guest UUID REFERENCES pixels.guest_sessions(id),
 login_session_id UUID,
 owner_revision BIGINT NOT NULL CHECK (owner_revision>0),
 client_type TEXT NOT NULL CHECK (client_type IN ('panel','android','user_web')),
 access_role TEXT NOT NULL CHECK (access_role IN ('controller','observer')),
 request_id UUID NOT NULL,
 request_hash BYTEA NOT NULL CHECK (octet_length(request_hash)=32),
 state TEXT NOT NULL DEFAULT 'pending' CHECK (state IN ('pending','connected','closing','reconcile_required','closed')),
 revision BIGINT NOT NULL DEFAULT 1 CHECK (revision>0),
 node_generation BIGINT NOT NULL CHECK (node_generation>0),
 control_epoch BIGINT NOT NULL REFERENCES pixels.control_runs(epoch),
 endpoint_revision BIGINT NOT NULL CHECK (endpoint_revision>0),
 descriptor_hash BYTEA CHECK (octet_length(descriptor_hash)=32),
 descriptor_expires_at TIMESTAMPTZ,
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 closed_at TIMESTAMPTZ,
 UNIQUE(id,node_id),
 FOREIGN KEY(node_id,device_id) REFERENCES pixels.nodes(id,device_id),
 FOREIGN KEY(instance_id,application_id,node_id) REFERENCES pixels.instances(id,application_id,node_id),
 FOREIGN KEY(login_session_id,owner_user) REFERENCES pixels.login_sessions(id,user_id),
 CHECK ((owner_user IS NULL)<>(owner_guest IS NULL)),
 CHECK ((login_session_id IS NULL)=(owner_user IS NULL)),
 CHECK ((target_kind='desktop' AND device_id IS NOT NULL AND application_id IS NULL AND instance_id IS NULL AND owner_user IS NOT NULL)
  OR (target_kind='cloud_application' AND device_id IS NULL AND application_id IS NOT NULL AND instance_id IS NOT NULL)),
 CHECK ((closed_at IS NOT NULL)=(state='closed')),
 CHECK ((descriptor_hash IS NULL)=(descriptor_expires_at IS NULL))
);
CREATE UNIQUE INDEX resource_sessions_user_request ON pixels.resource_sessions(owner_user,request_id) WHERE owner_user IS NOT NULL;
CREATE UNIQUE INDEX resource_sessions_guest_request ON pixels.resource_sessions(owner_guest,request_id) WHERE owner_guest IS NOT NULL;
CREATE UNIQUE INDEX resource_sessions_instance_controller ON pixels.resource_sessions(instance_id) WHERE access_role='controller' AND closed_at IS NULL;
CREATE UNIQUE INDEX resource_sessions_desktop_controller ON pixels.resource_sessions(device_id) WHERE access_role='controller' AND closed_at IS NULL;
CREATE INDEX resource_sessions_node_active ON pixels.resource_sessions(node_id,id) WHERE closed_at IS NULL;
CREATE INDEX resource_sessions_owner_user ON pixels.resource_sessions(owner_user,id);
CREATE INDEX resource_sessions_owner_guest ON pixels.resource_sessions(owner_guest,id);
CREATE TABLE pixels.resource_session_events (
 id UUID PRIMARY KEY,
 session_id UUID NOT NULL REFERENCES pixels.resource_sessions(id),
 revision BIGINT NOT NULL CHECK (revision>0),
 kind TEXT NOT NULL CHECK (kind IN ('created','descriptor','connected','closing','reconcile_required','closed')),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(session_id,revision,kind)
);
-- A challenge is a request to persist an inclusive admission fence and retire this
-- exact frontend identity, not a request to stop its app or Windows workspace.
CREATE TABLE pixels.resource_session_retirements (
 session_id UUID PRIMARY KEY,
 node_id UUID NOT NULL,
 challenge_id UUID NOT NULL UNIQUE,
 session_revision BIGINT NOT NULL CHECK (session_revision>0),
 node_generation BIGINT NOT NULL CHECK (node_generation>0),
 control_epoch BIGINT NOT NULL REFERENCES pixels.control_runs(epoch),
 deadline TIMESTAMPTZ NOT NULL,
 completed_at TIMESTAMPTZ,
 FOREIGN KEY(session_id,node_id) REFERENCES pixels.resource_sessions(id,node_id)
);
GRANT SELECT,INSERT ON pixels.resource_sessions,pixels.resource_session_events,pixels.resource_session_retirements TO pixels_console_runtime;
GRANT UPDATE(state,revision,descriptor_hash,descriptor_expires_at,closed_at) ON pixels.resource_sessions TO pixels_console_runtime;
GRANT UPDATE(challenge_id,session_revision,node_generation,control_epoch,deadline,completed_at) ON pixels.resource_session_retirements TO pixels_console_runtime;
