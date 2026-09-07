#Requires -Version 5.1
$ErrorActionPreference = "Continue"

$VcpkgRoot = "C:/source/vcpkg"
$Triplet = "x64-windows-static-release"
$ProxyUrl = "http://127.0.0.1:7890"

$env:HTTP_PROXY  = $ProxyUrl
$env:HTTPS_PROXY = $ProxyUrl
$env:NO_PROXY    = "localhost,127.0.0.1"

# Build host tools with the same static-release triplet to avoid extra dynamic CRT builds
$env:VCPKG_DEFAULT_HOST_TRIPLET = $Triplet

# Direct vcpkg dependencies (Qt remains official /MD)
$Packages = @(
    "abseil",
    "amd-amf",
    "aom",
    "atl",
    "breakpad",
    "bzip2[tool]",
    "cpr[ssl]",
    "curl[non-http,ssl,sspi]",
    "dav1d",
    "detours",
    "directxmath",
    "directxtex[dx11]",
    "dirent",
    "egl-registry",
    "eigen3",
    "expat",
    "ffmpeg[amf,aom,avcodec,avdevice,avfilter,avformat,dav1d,ffmpeg,ffplay,ffprobe,freetype,gpl,iconv,ilbc,lzma,modplug,mp3lame,nvcodec,opencl,opengl,openh264,openjpeg,openmpt,openssl,opus,qsv,sdl2,snappy,soxr,speex,srt,ssh,swresample,swscale,theora,version3,vorbis,vpx,vulkan,webp,x264,x265,xml2,zlib]",
    "ffnvcodec",
    "fftw3",
    "flatbuffers",
    "fmt",
    "freetype[brotli,bzip2,png,zlib]",
    "gflags",
    "glm",
    "gtest",
    "imath",
    "leveldb",
    "libdeflate[compression,gzip,zlib]",
    "libdisasm",
    "libiconv",
    "libilbc",
    "libjpeg-turbo",
    "liblzma",
    "libmodplug",
    "libogg",
    "libopenmpt",
    "libpng",
    "libsrt",
    "libssh[pcap,server]",
    "libtheora",
    "libtwolame",
    "libvorbis",
    "libvpl",
    "libvpx",
    "libwebp[libwebpmux,nearlossless,simd,unicode]",
    "libxml2[iconv,zlib]",
    "libyuv",
    "miniz",
    "mp3lame",
    "mpg123",
    "nlohmann-json",
    "opencl",
    "opencv4[calib3d,contrib,dshow,eigen,ffmpeg,gapi,highgui,ipp,jpeg,msmf,openexr,openmp,png,quirc,tiff,webp,world]",
    "openexr",
    "opengl-registry",
    "opengl",
    "openh264",
    "openjpeg",
    "openjph",
    "openssl",
    "opus",
    "protobuf",
    "quirc",
    "sdl2",
    "snappy",
    "soxr",
    "spdlog[fmt,tz-offset]",
    "speex",
    "sqlite-orm",
    "sqlite3[json1]",
    "stb",
    "tiff[jpeg,lzma,zip]",
    "tomlplusplus",
    "utf8-range",
    "vulkan",
    "x264[asm,gpl]",
    "x265",
    "zlib"
)

$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
$Success = @()
$Failed = @()

foreach ($Pkg in $Packages) {
    $Spec = "$Pkg`:$Triplet"
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Installing $Spec" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    & $VcpkgExe install $Spec
    if ($LASTEXITCODE -eq 0) {
        $Success += $Spec
        Write-Host "SUCCESS: $Spec" -ForegroundColor Green
    }
    else {
        $Failed += $Spec
        Write-Host "FAILED: $Spec" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Install summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Succeeded ($($Success.Count)):" -ForegroundColor Green
$Success | ForEach-Object { Write-Host "  $_" -ForegroundColor Green }
Write-Host ""
Write-Host "Failed ($($Failed.Count)):" -ForegroundColor Red
$Failed | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
