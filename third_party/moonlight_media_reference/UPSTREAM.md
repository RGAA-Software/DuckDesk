# Pinned, unmodified media reference

Source: https://github.com/moonlight-stream/moonlight-common-c
Revision: e41355ea01670fd4c830b384009d31dd0339a705
Selected by Sunshine 3cba9baebac882b336be3ebe129ee612cb189853.

`src/` and `LICENSE.txt` are unchanged upstream files. Only the selected receive
queue sources are compiled into isolated reference tests, not the product.

`nanors/` contains unchanged `rs.c`, `rs.h`, `deps/` and `LICENSE` from
https://github.com/sleepybishop/nanors at b1e3c22ca0cdc0bb83e3cd6ed1a2fc77869ed99a.
The product adapter may link this independent MIT-licensed codec. Exported C
symbols are prefixed using compiler definitions to avoid collisions with the
retained legacy codec; upstream files are not patched.

`enet/include/` and `enet/LICENSE` come from https://github.com/cgutman/enet at
aca87840b57f045a1f7f9299e4b1b9b8e2a5e2f1. Headers are needed by the reference
queue's upstream umbrella include. No ENet transport is linked into the product.

The C++ adapters and test harness live in `src/px_client_sdk/media_transport/`.
They are project-maintained and subject to the project's lifetime standard.
