SELECT user_id,id AS session_id,authorization_revision,client_type FROM pixels.login_sessions WHERE token_hash=$1
