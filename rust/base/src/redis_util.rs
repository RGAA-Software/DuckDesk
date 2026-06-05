use redis::aio::ConnectionManager;
use redis::RedisResult;

pub async fn get_redis_conn_mgr(redis_url: String) -> RedisResult<ConnectionManager> {
    let redis_client = redis::Client::open(redis_url.clone())?;
    redis_client.get_connection_manager().await
}