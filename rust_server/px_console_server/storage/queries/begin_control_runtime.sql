WITH next AS (UPDATE pixels.control_runtime SET epoch=epoch+1 RETURNING epoch)
INSERT INTO pixels.control_runs(epoch,instance_id) SELECT epoch,$1 FROM next RETURNING epoch
