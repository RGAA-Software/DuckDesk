### Windows

#### 1. Open CMD
!!! MUST USE THIS !!!  
x64 Native Tools Command Prompt for VS 2022

#### 2. Build
##### 2.1 OpenSource
> build_opensource.bat

##### 2.2 Official
> build_official.bat

#### 3. Local validation delivery rule

The Windows client is launched and validated from `build_official\dist`. A successful
compile alone is not a completed local delivery: synchronize all changed runtime
artifacts into `build_official\dist` and verify the source and dist SHA-256 hashes
match before asking for validation.
