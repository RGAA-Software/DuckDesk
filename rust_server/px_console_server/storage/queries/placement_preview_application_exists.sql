SELECT statement_timestamp() AS "evaluated_at!",
 EXISTS(SELECT 1 FROM pixels.applications WHERE id=$1) AS "exists!"
