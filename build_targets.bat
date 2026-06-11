@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d D:\source\GoCloud\GammaRayPremium\build_official
ninja GammaRayClientInner GammaRay tc_protection 2>&1
