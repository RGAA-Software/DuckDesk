use std::sync::Arc;
use redis::aio::MultiplexedConnection;
use redis::aio::ConnectionLike;
use tokio::sync::Mutex;

pub struct RelayRedisConn <T> where T: ConnectionLike, T: Clone {
    conn: Option<T>
}

impl <T> RelayRedisConn<T> where T: ConnectionLike, T: Clone {
    pub fn new() -> Self {
        Self {
            conn: None
        }
    }

    pub fn set_conn(&mut self, conn: T) {
        self.conn = Some(conn);
    }

    pub fn clone_conn(&self) -> T {
        self.conn.clone().unwrap()
    }

    pub fn ref_conn(&self) -> &T {
        self.conn.as_ref().unwrap()
    }
}