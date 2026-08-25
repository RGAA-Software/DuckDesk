# Workspace delivery rules

- The user starts and validates the Windows client from `build_official\dist`.
- A client-side build is not considered delivered until every changed runtime artifact (including executables, DLLs, language resources, and web assets when applicable) has been synchronized into `build_official\dist`.
- Before reporting a build ready for validation, verify that the relevant build-tree artifacts and their `build_official\dist` copies have matching SHA-256 hashes. If a destination file is in use, stop the corresponding process, publish the artifact, and re-run the hash check.
