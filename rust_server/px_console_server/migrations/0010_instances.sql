ALTER TABLE pixels.application_deployments ADD CONSTRAINT deployments_identity UNIQUE(id,application_id,node_id,kind);
ALTER TABLE pixels.login_sessions ADD CONSTRAINT login_sessions_id_user UNIQUE(id,user_id);
CREATE TABLE pixels.instances (
 id UUID PRIMARY KEY,
 application_id UUID NOT NULL,
 deployment_id UUID NOT NULL,
 node_id UUID NOT NULL,
 kind TEXT NOT NULL CHECK (kind IN ('game_hook','webview','rdp')),
 owner_user UUID REFERENCES pixels.users(id),
 owner_guest UUID REFERENCES pixels.guest_sessions(id),
 login_session_id UUID,
 owner_revision BIGINT NOT NULL CHECK (owner_revision>0),
 client_type TEXT NOT NULL CHECK (client_type IN ('panel','android','user_web')),
 request_id UUID NOT NULL,
 request_hash BYTEA NOT NULL CHECK (octet_length(request_hash)=32),
 launch_id UUID NOT NULL UNIQUE,
 state TEXT NOT NULL DEFAULT 'reserved' CHECK (state IN ('reserved','starting','running','stopping','reconcile_required','stopped','failed')),
 desired_state TEXT NOT NULL DEFAULT 'running' CHECK (desired_state IN ('running','stopped')),
 revision BIGINT NOT NULL DEFAULT 1 CHECK (revision>0),
 application_revision BIGINT NOT NULL CHECK (application_revision>0),
 application_access_revision BIGINT NOT NULL CHECK (application_access_revision>0),
 deployment_revision BIGINT NOT NULL CHECK (deployment_revision>0),
 node_generation BIGINT NOT NULL CHECK (node_generation>0),
 control_epoch BIGINT NOT NULL REFERENCES pixels.control_runs(epoch),
 endpoint_revision BIGINT NOT NULL CHECK (endpoint_revision>0),
 port INTEGER NOT NULL CHECK (port>=1 AND port<=65535),
 install_root TEXT,
 executable_relative TEXT,
 arguments TEXT,
 entry_url TEXT,
 codec TEXT CHECK (codec IN ('h264','h265')),
 bitrate_kbps INTEGER CHECK (bitrate_kbps>=128 AND bitrate_kbps<=200000),
 gpu_key TEXT,
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 started_at TIMESTAMPTZ,
 ended_at TIMESTAMPTZ,
 UNIQUE(id,node_id),
 FOREIGN KEY(deployment_id,application_id,node_id,kind) REFERENCES pixels.application_deployments(id,application_id,node_id,kind),
 FOREIGN KEY(login_session_id,owner_user) REFERENCES pixels.login_sessions(id,user_id),
 CHECK ((login_session_id IS NULL)=(owner_user IS NULL)),
 CHECK ((owner_user IS NULL)<>(owner_guest IS NULL)),
 CHECK ((ended_at IS NOT NULL)=(state IN ('stopped','failed'))),
 CHECK ((kind='game_hook' AND install_root IS NOT NULL AND executable_relative IS NOT NULL AND arguments IS NOT NULL AND entry_url IS NULL AND codec IS NOT NULL AND bitrate_kbps IS NOT NULL)
 OR (kind='webview' AND install_root IS NULL AND executable_relative IS NULL AND arguments IS NULL AND entry_url IS NOT NULL AND codec IS NOT NULL AND bitrate_kbps IS NOT NULL)
 OR (kind='rdp' AND install_root IS NULL AND executable_relative IS NULL AND arguments IS NULL AND entry_url IS NULL AND codec IS NULL AND bitrate_kbps IS NULL))
);
CREATE UNIQUE INDEX instances_user_request ON pixels.instances(owner_user,request_id) WHERE owner_user IS NOT NULL;
CREATE UNIQUE INDEX instances_guest_request ON pixels.instances(owner_guest,request_id) WHERE owner_guest IS NOT NULL;
CREATE UNIQUE INDEX instances_active_port ON pixels.instances(node_id,port) WHERE ended_at IS NULL;
CREATE UNIQUE INDEX instances_rdp_busy ON pixels.instances(application_id,node_id) WHERE kind='rdp' AND ended_at IS NULL;
CREATE INDEX instances_deployment_active ON pixels.instances(deployment_id,id) WHERE ended_at IS NULL;
CREATE INDEX instances_owner_user ON pixels.instances(owner_user,id);
CREATE INDEX instances_owner_guest ON pixels.instances(owner_guest,id);
CREATE TABLE pixels.instance_commands (
 id UUID PRIMARY KEY,
 instance_id UUID NOT NULL REFERENCES pixels.instances(id),
 node_id UUID NOT NULL REFERENCES pixels.nodes(id),
 node_generation BIGINT NOT NULL CHECK (node_generation>0),
 control_epoch BIGINT NOT NULL REFERENCES pixels.control_runs(epoch),
 instance_revision BIGINT NOT NULL CHECK (instance_revision>0),
 kind TEXT NOT NULL CHECK (kind IN ('start','stop')),
 state TEXT NOT NULL DEFAULT 'pending' CHECK (state IN ('pending','claimed','completed','cancelled')),
 attempts INTEGER NOT NULL DEFAULT 0 CHECK (attempts>=0),
 lease_id UUID,
 lease_until TIMESTAMPTZ,
 deadline TIMESTAMPTZ NOT NULL,
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 completed_at TIMESTAMPTZ,
 outcome TEXT CHECK (outcome IN ('running','absent','unknown')),
 completed_lease_id UUID,
 FOREIGN KEY(instance_id,node_id) REFERENCES pixels.instances(id,node_id),
 CHECK ((lease_id IS NULL)=(lease_until IS NULL)),
 CHECK ((state='claimed')=(lease_id IS NOT NULL)),
 CHECK ((completed_at IS NOT NULL)=(state IN ('completed','cancelled'))),
 CHECK ((outcome IS NOT NULL)=(state='completed')),
 CHECK ((completed_lease_id IS NOT NULL)=(state='completed')),
 UNIQUE(instance_id,instance_revision,kind)
);
CREATE INDEX instance_commands_dispatch ON pixels.instance_commands(node_id,state,created_at,id);
CREATE TABLE pixels.instance_events (
 id UUID PRIMARY KEY,
 instance_id UUID NOT NULL REFERENCES pixels.instances(id),
 revision BIGINT NOT NULL CHECK (revision>0),
 kind TEXT NOT NULL CHECK (kind IN ('reserved','starting','running','stop_requested','reconcile_required','reconciled','stopped','failed')),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(instance_id,revision)
);
CREATE TABLE pixels.instance_admin_actions (
 id UUID PRIMARY KEY,
 instance_id UUID NOT NULL REFERENCES pixels.instances(id),
 actor_id UUID NOT NULL REFERENCES pixels.users(id),
 requested_revision BIGINT NOT NULL,
 result_revision BIGINT NOT NULL CHECK (result_revision>0),
 action TEXT NOT NULL CHECK (action='stop'),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp()
);
GRANT SELECT,INSERT ON pixels.instances,pixels.instance_commands,pixels.instance_events,pixels.instance_admin_actions TO pixels_console_runtime;
GRANT UPDATE(state,desired_state,revision,node_generation,control_epoch,endpoint_revision,started_at,ended_at) ON pixels.instances TO pixels_console_runtime;
GRANT UPDATE(state,instance_revision,attempts,lease_id,lease_until,completed_at,outcome,completed_lease_id) ON pixels.instance_commands TO pixels_console_runtime;
