# Frame-rate synchronization archive

Base revision: `5d90f4de288d0cd85166c0f75646ec3d9c43b873`.
Full working files retained before replacing capture-only frame-rate updates with shared encoder admission policy.
Original paths are preserved under this directory. `ingress/network_event_ingress.cpp` and `tests/CMakeLists.txt`
already had local modifications; the other four files were clean. Archive is reference-only, not built or packaged.

- `src/px_render/app/encoder_thread.h`: C2A443F0C40432B9C326B646963BE971E850CA4928FFB3F365F60DB1A40D5280
- `src/px_render/app/encoder_thread.cpp`: 8B009C050279DEDFDBA0290139EEB625A63282AD7E4D266788FAB225C4353484
- `src/px_render/rd_app.h`: D7C3414547C68A6F1ACED8A81BE41694B09167488B5265D93FCC51B0BCCD5989
- `src/px_render/rd_app.cpp`: BA9716CF3FCBD8BB752D4753312F73914F298A0AA36C0A159481221F0C69C4AD
- `src/px_render/ingress/network_event_ingress.cpp`: C15391CAFFDBE3DEED08405F19AF5E771D7ECE89A4534A0000E7A72248594E41
- `src/px_render/tests/CMakeLists.txt`: 3091B880DA1B85D2B59D56C0A724CED0F019607E5B6F21E1A7B3E7A600A1EB5E
