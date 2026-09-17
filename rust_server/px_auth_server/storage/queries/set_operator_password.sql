UPDATE pixels.authors SET password_hash=$1,authorization_revision=authorization_revision+1
             WHERE id=$2 AND authorization_revision=$3
