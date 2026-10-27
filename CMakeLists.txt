cmake_minimum_required(VERSION 3.20)
project(mcpqt_websearch VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

# ----- C++ / Qt boilerplate ------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Plugins are QObject-derived Qt plugins, so moc must run.
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

# ----- HTML parser selection -----------------------------------------------
# Fetched page content is always extracted with the vendored Lexbor parser.
# Search result pages are always parsed with the QXmlStreamReader-backed parser.
set(LEXBOR_BUILD_SHARED     OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_STATIC     ON  CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_UTILS      OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory(lexbor EXCLUDE_FROM_ALL)

# ----- ddg core sources ----------------------------------------------------
set(DDG_CORE_SOURCES
    src/ddg_core/fetcher.cpp
    src/ddg_core/host_rate_limiter.cpp
    src/ddg_core/webio_transport.cpp
    src/ddg_core/link_registry.cpp
    src/ddg_core/safe_search_mode.cpp
    src/ddg_core/searcher.cpp
    src/ddg_core/rate_limiter.cpp
    src/ddg_core/ttl_cache.cpp
    src/ddg_core/html_document.cpp
    src/ddg_core/html_extract.cpp
    src/ddg_core/html_query.cpp
    src/ddg_core/html_text.cpp
    src/ddg_core/page_parser_xml.cpp
    src/ddg_core/readability.cpp
    src/ddg_core/utils.cpp
    src/ddg_core/xml_dom.cpp
)

set(DDG_CORE_HEADERS
    src/ddg_core/constants.hpp
    src/ddg_core/error.hpp
    src/ddg_core/fetcher.hpp
    src/ddg_core/host_rate_limiter.hpp
    src/ddg_core/html_document.hpp
    src/ddg_core/html_extract.hpp
    src/ddg_core/html_query.hpp
    src/ddg_core/html_text.hpp
    src/ddg_core/http_transport.hpp
    src/ddg_core/http_response.hpp
    src/ddg_core/link_registry.hpp
    src/ddg_core/log.hpp
    src/ddg_core/rate_limiter.hpp
    src/ddg_core/readability.hpp
    src/ddg_core/safe_search_mode.hpp
    src/ddg_core/searcher.hpp
    src/ddg_core/search_result.hpp
    src/ddg_core/ttl_cache.hpp
    src/ddg_core/utils.hpp
    src/ddg_core/webio_transport.hpp
    src/ddg_core/xml_dom.hpp
)

add_library(mcpqt_websearch MODULE
    src/websearch_plugin.cpp
    src/plugin.cpp
    src/plugin.h
    ${DDG_CORE_SOURCES}
    ${DDG_CORE_HEADERS}
)
set_target_properties(mcpqt_websearch PROPERTIES PREFIX "")

target_include_directories(mcpqt_websearch PRIVATE
    src
    src/ddg_core
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

# ----- tests ---------------------------------------------------------------
if(BUILD_TESTING)
    add_executable(ddg_html_to_markdown_test
        tests/html_to_markdown_test.cpp
        src/ddg_core/html_extract.cpp
        src/ddg_core/html_document.cpp
        src/ddg_core/html_query.cpp
        src/ddg_core/html_text.cpp
        src/ddg_core/readability.cpp
        src/ddg_core/utils.cpp
    )
    target_include_directories(ddg_html_to_markdown_test PRIVATE
        src
        src/ddg_core
    )
    target_link_libraries(ddg_html_to_markdown_test PRIVATE
        Qt6::Core
        Qt6::Network
        lexbor_static
    )

    add_test(NAME ddg_html_to_markdown
        COMMAND ddg_html_to_markdown_test
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/ddg_lite_results.html
    )

    add_executable(ddg_page_parser_xml_test
        tests/page_parser_xml_test.cpp
        src/ddg_core/page_parser_xml.cpp
        src/ddg_core/xml_dom.cpp
    )
    target_include_directories(ddg_page_parser_xml_test PRIVATE
        src
        src/ddg_core
    )
    target_link_libraries(ddg_page_parser_xml_test PRIVATE
        Qt6::Core
    )
    add_test(NAME ddg_page_parser_xml
        COMMAND ddg_page_parser_xml_test
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/ddg_lite_results.html
    )

    add_executable(webio_transport_test
        tests/webio_transport_test.cpp
        src/ddg_core/webio_transport.cpp
    )
    target_include_directories(webio_transport_test PRIVATE
        src
        src/ddg_core
    )
    target_link_libraries(webio_transport_test PRIVATE
        Qt6::Core
    )
    add_test(NAME webio_transport COMMAND webio_transport_test)
endif()
