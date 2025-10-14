# ---------------------
# Selecting SSL backend
# ---------------------

# Default: OpenSSL
option(WFX_HTTP_USE_OPENSSL "Use OpenSSL as HTTPS backend" ON)
# option(WFX_HTTP_USE_WOLFSSL "Use WolfSSL as HTTPS backend" OFF)
# option(WFX_HTTP_USE_MBEDTLS "Use mbedTLS as HTTPS backend" OFF)

if(WFX_HTTP_USE_OPENSSL)
    include(${CMAKE_CURRENT_LIST_DIR}/ssl_openssl.cmake)

    target_include_directories(wfx PRIVATE ${OPENSSL_INSTALL_DIR}/include)

    target_compile_definitions(wfx PRIVATE WFX_HTTP_USE_OPENSSL)
    target_link_libraries(wfx PRIVATE OpenSSL::SSL)
else()
    message(FATAL_ERROR "No SSL backend selected!")
endif()