cmake_minimum_required(VERSION 3.22)
project(cforgev_android CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimizacion
add_compile_options(-O2 -fno-exceptions -fno-rtti)
# Pero cforgev necesita excepciones C++
set_source_files_properties(cforgev.cpp PROPERTIES COMPILE_FLAGS "-fexceptions -frtti")
set_source_files_properties(cforgev_jni.cpp PROPERTIES COMPILE_FLAGS "-fexceptions")

# Libreria nativa que carga Android
add_library(cforgev_jni SHARED
    cforgev_jni.cpp
    cforgev.cpp          # Copiar cforgev.cpp del repo raiz aqui
)

# Android system libs
find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(cforgev_jni
    ${log-lib}
    ${android-lib}
)

# OpenSSL para Android (opcional — descargar de https://github.com/217heidai/openssl_for_android)
# Si tienes OpenSSL precompilado para Android:
# set(OPENSSL_DIR ${CMAKE_SOURCE_DIR}/openssl/${ANDROID_ABI})
# target_compile_definitions(cforgev_jni PRIVATE CFV_WITH_OPENSSL)
# target_include_directories(cforgev_jni PRIVATE ${OPENSSL_DIR}/include)
# target_link_libraries(cforgev_jni ${OPENSSL_DIR}/libssl.a ${OPENSSL_DIR}/libcrypto.a)
