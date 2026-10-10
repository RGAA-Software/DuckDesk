//! Product Relay data plane. Resource-session authorization remains at Render.
mod admission;
mod config;
mod runtime;

pub use config::IrohRelayConfig;
pub use runtime::{IrohRelay, IrohRelayManagement};

#[cfg(test)]
mod tests;
