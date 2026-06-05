use sled::{Db, IVec};

pub struct KvStorage {
    db: Option<Db>,
}

impl KvStorage {
    pub fn new() -> Self {
        Self {
            db: None,
        }
    }

    pub fn init(&mut self, name: &str) -> bool {
        let current_dir = std::env::current_exe().unwrap();
        let current_dir = current_dir.parent().unwrap();
        let path = current_dir.join(name);

        let db = sled::open(path).expect("open");
        self.db = Some(db);
        true
    }

    pub fn put(&self, key: &str, value: &str) -> bool {
        if let Some(db) = &self.db {
            if let Ok(_r) = db.insert(key, value.as_bytes()) {
                let _ = db.flush();
                return true;
            }
        }
        false
    }

    pub fn get(&self, key: &str) -> Option<String> {
        if let Some(db) = &self.db {
            return db
                .get(key)
                .unwrap()
                .map(|v: IVec| {
                    String::from_utf8(v.to_vec()).unwrap()
                });
        }
        None
    }

    pub fn del(&self, key: &str) -> bool {
        if let Some(db) = &self.db {
            if let Err(_e) = db.remove(key) {
                return false;
            }
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_kvstore() {
        let mut kv = KvStorage::new();
        kv.init("test");

        kv.put("hello", "world");
        println!("v: {}", kv.get("hello").unwrap());
        assert_eq!(kv.get("hello").unwrap(), "world");

        kv.del("hello");
        assert!(kv.get("hello").is_none());
    }
}
