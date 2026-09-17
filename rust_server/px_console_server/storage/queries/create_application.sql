INSERT INTO pixels.applications(id,name,kind,access_mode,entry_url,executable_relative,arguments,bitrate_kbps,codec,allow_observer,allow_takeover,disabled)
VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12) RETURNING id,name,kind,access_mode,entry_url,executable_relative,arguments,bitrate_kbps,codec,allow_observer,allow_takeover,disabled,revision,access_revision
