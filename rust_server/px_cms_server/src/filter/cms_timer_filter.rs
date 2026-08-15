use axum::body::Body;
use axum::http::Request;
use axum::middleware::Next;
use axum::response::Response;

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    //println!("--> hit {}", req.uri().path());
    next.run(req).await
}
