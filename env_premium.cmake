set(BUILD_PREMIUM ON)
set(VCPKG_ROOT C:/source/vcpkg)
if(NOT VCPKG_TARGET_TRIPLET)
    # Use the same dynamic triplet as the rest of the vcpkg dependencies
    set(VCPKG_TARGET_TRIPLET x64-windows)
endif()
#set(QT_ROOT D:/Qt6.8/6.8.0/msvc2022_64)
set(QT_ROOT C:/Qt6.8.3/6.8.3/msvc2022_64)
set(VK_SDK_ROOT C:/VulkanSDK/1.3.290.0)