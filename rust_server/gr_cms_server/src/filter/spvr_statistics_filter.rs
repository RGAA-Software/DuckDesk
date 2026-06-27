use axum::body::Body;
use axum::http::Request;
use axum::middleware::Next;
use axum::response::Response;

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    //println!("auth: --> hit {}", req.uri());
    if let Some(_query) = req.uri().query() {
        //println!("--> query: {:?}", query);
    }
    next.run(req).await
}
