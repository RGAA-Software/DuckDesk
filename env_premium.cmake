set(BUILD_PREMIUM ON)
set(VCPKG_ROOT C:/source/vcpkg)
if(NOT VCPKG_TARGET_TRIPLET)
    # Use static CRT (/MT) release triplet to match project-wide static linking
    set(VCPKG_TARGET_TRIPLET x64-windows-static-release)
endif()
# Use vcpkg's Vulkan headers/loader (newer than installed SDK) so extensions
# such as VK_KHR_video_decode_av1 are available at compile time.
set(VK_SDK_ROOT ${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET})
set(ENV{VULKAN_SDK} ${VK_SDK_ROOT})
