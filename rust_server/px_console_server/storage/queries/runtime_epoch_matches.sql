SELECT EXISTS(SELECT 1 FROM pixels.control_runtime WHERE epoch=$1) AS "current!"
