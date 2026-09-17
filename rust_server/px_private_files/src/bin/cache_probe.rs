use px_private_files::CacheRoot;
use std::{
    io::{Read, Write},
    path::Path,
};
fn main() {
    let args: Vec<_> = std::env::args().collect();
    if args.len() != 3 {
        std::process::exit(2);
    }
    let deployment = match args[2].parse() {
        Ok(id) => id,
        Err(_) => std::process::exit(2),
    };
    let _root = match CacheRoot::open(Path::new(&args[1]), deployment) {
        Ok(root) => root,
        Err(_) => std::process::exit(3),
    };
    println!("READY");
    std::io::stdout().flush().unwrap();
    let mut byte = [0_u8; 1];
    let _ = std::io::stdin().read(&mut byte);
}
