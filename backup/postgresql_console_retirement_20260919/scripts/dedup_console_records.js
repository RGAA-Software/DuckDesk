// scripts/dedup_console_records.js
// Usage: mongosh localhost:27017/db_gr_console_server scripts/dedup_console_records.js

function dedup(collectionName, idField) {
    const src = db.getCollection(collectionName);
    const tmpName = collectionName + "_dedup_tmp_" + Date.now();
    const tmp = db.getCollection(tmpName);
    tmp.drop();

    src.aggregate([
        { $sort: { created_timestamp: -1 } },
        { $group: { _id: "$" + idField, doc: { $first: "$$ROOT" } } },
        { $replaceRoot: { newRoot: "$doc" } },
        { $out: tmpName }
    ]);

    src.drop();
    tmp.renameCollection(collectionName);
    print("Deduped " + collectionName + " by " + idField);
}

dedup("c_visit", "conn_id");
dedup("c_file_transfer", "the_file_id");
