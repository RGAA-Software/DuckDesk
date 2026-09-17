ALTER TABLE pixels.applications ADD CONSTRAINT applications_id_kind UNIQUE(id,kind);
CREATE TABLE pixels.application_deployments (
 id UUID PRIMARY KEY,
 application_id UUID NOT NULL,
 node_id UUID NOT NULL REFERENCES pixels.nodes(id),
 kind TEXT NOT NULL CHECK (kind IN ('game_hook','webview','rdp')),
 install_root TEXT,
 gpu_key TEXT CHECK (char_length(gpu_key)>=1 AND char_length(gpu_key)<=128),
 capacity INTEGER NOT NULL CHECK (capacity>=1 AND capacity<=64),
 disabled BOOLEAN NOT NULL,
 revision BIGINT NOT NULL DEFAULT 1 CHECK (revision>0),
 application_revision BIGINT NOT NULL CHECK (application_revision>0),
 observed_state TEXT NOT NULL DEFAULT 'pending' CHECK (observed_state IN ('pending','ready','failed')),
 observed_reason TEXT CHECK (observed_reason IN ('missing_files','unsupported_mode','binding_unverified','invalid_configuration','dependency_unavailable')),
 observed_generation BIGINT CHECK (observed_generation>0),
 observed_epoch BIGINT REFERENCES pixels.control_runs(epoch),
 observed_endpoint_revision BIGINT CHECK (observed_endpoint_revision>0),
 observed_sequence BIGINT NOT NULL DEFAULT 0 CHECK (observed_sequence>=0),
 observed_at TIMESTAMPTZ,
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(application_id,node_id),
 FOREIGN KEY(application_id,kind) REFERENCES pixels.applications(id,kind),
 CHECK ((kind='game_hook' AND install_root IS NOT NULL AND char_length(install_root)>=3 AND char_length(install_root)<=2048) OR (kind<>'game_hook' AND install_root IS NULL)),
 CHECK (kind<>'rdp' OR capacity=1),
 CHECK ((observed_state='failed')=(observed_reason IS NOT NULL)),
 CHECK ((observed_sequence=0)=(observed_at IS NULL)),
 CHECK ((observed_generation IS NULL)=(observed_epoch IS NULL)),
 CHECK ((observed_generation IS NULL)=(observed_endpoint_revision IS NULL)),
 CHECK ((observed_sequence=0)=(observed_generation IS NULL)),
 CHECK (observed_sequence<>0 OR observed_state='pending')
);
CREATE INDEX deployments_node ON pixels.application_deployments(node_id,id);
CREATE TABLE pixels.deployment_audit (
 id UUID PRIMARY KEY,
 deployment_id UUID NOT NULL REFERENCES pixels.application_deployments(id),
 actor_id UUID NOT NULL REFERENCES pixels.users(id),
 revision BIGINT NOT NULL CHECK (revision>0),
 action TEXT NOT NULL CHECK (action IN ('created','configured')),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(deployment_id,revision)
);
GRANT SELECT,INSERT ON pixels.application_deployments,pixels.deployment_audit TO pixels_console_runtime;
GRANT UPDATE(install_root,gpu_key,capacity,disabled,revision,application_revision,observed_state,observed_reason,
 observed_generation,observed_epoch,observed_endpoint_revision,observed_sequence,observed_at)
 ON pixels.application_deployments TO pixels_console_runtime;
