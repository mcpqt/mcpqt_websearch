cmake_minimum_required(VERSION 3.20)
project(mcpqt_websearch VERSION 0.1.0 LANGUAGES C CXX)

# ----- C++ / Qt boilerplate ------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Plugins are QObject-derived Qt plugins, so moc must run.
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

# ----- vendored lexbor (HTML parser used by the ddg core) -------------------
set(LEXBOR_BUILD_SHARED     OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_STATIC     ON  CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_UTILS      OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory(lexbor EXCLUDE_FROM_ALL)

# ----- ddg core sources ----------------------------------------------------
set(DDG_CORE_SOURCES
    src/ddg_core/context.cpp
    src/ddg_core/fetcher.cpp
    src/ddg_core/host_rate_limiter.cpp
    src/ddg_core/html_document.cpp
    src/ddg_core/html_extract.cpp
    src/ddg_core/html_query.cpp
    src/ddg_core/html_text.cpp
    src/ddg_core/http_client.cpp
    src/ddg_core/http_client_qt.cpp
    src/ddg_core/link_registry.cpp
    src/ddg_core/rate_limiter_factory.cpp
    src/ddg_core/safe_search_mode.cpp
    src/ddg_core/searcher.cpp
    src/ddg_core/sliding_rate_limiter.cpp
    src/ddg_core/token_bucket_limiter.cpp
    src/ddg_core/ttl_cache.cpp
    src/ddg_core/utils.cpp
)

add_library(mcpqt_websearch MODULE
    src/websearch_plugin.cpp
    src/plugin.cpp
    ${DDG_CORE_SOURCES}
)
set_target_properties(mcpqt_websearch PROPERTIES PREFIX "")

target_include_directories(mcpqt_websearch PRIVATE
    src
    src/ddg_core
)

target_compile_definitions(mcpqt_websearch PRIVATE
    DDG_HTTP_BACKEND="qt"
)

target_link_libraries(mcpqt_websearch PRIVATE
    Qt6::Core
    Qt6::Network
    lexbor_static
)

target_compile_options(mcpqt_websearch PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
)
