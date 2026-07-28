# CMakeLists.txt para compilar cforgev como framework iOS (cross-compile)
#
# Uso:
#   mkdir build-ios && cd build-ios
#   cmake .. \
#     -DCMAKE_TOOLCHAIN_FILE=../ios.toolchain.cmake \
#     -DPLATFORM=OS64           \
#     -DCMAKE_BUILD_TYPE=Release
#   cmake --build . --config Release
#
# Requiere: ios-cmake toolchain de https://github.com/leetal/ios-cmake
# O usar directamente desde Xcode (ver README.md).

cmake_minimum_required(VERSION 3.22)
project(cforgev_ios CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Framework iOS estatico
add_library(cforgev STATIC
    CForgeApp/Sources/CForge/cforgev.cpp        # Copiar del repo raiz
    CForgeApp/Sources/CForge/CForgeRuntime.mm
)

target_include_directories(cforgev PUBLIC
    CForgeApp/Sources/CForge
)

target_compile_options(cforgev PRIVATE
    -fexceptions -frtti -O2
    -fobjc-arc
)

# Frameworks Apple necesarios
find_library(FOUNDATION_LIB Foundation)
find_library(UIKIT_LIB UIKit)

target_link_libraries(cforgev
    ${FOUNDATION_LIB}
    ${UIKIT_LIB}
)
