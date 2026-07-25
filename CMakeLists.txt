cmake_minimum_required(VERSION 3.24)

project(TradingViewChart VERSION 0.1.0 LANGUAGES CXX)

option(TRADINGVIEW_CHART_BUILD_TESTS "Build the unit test suite" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Widgets
    WebChannel
    WebEngineCore
    WebEngineWidgets
)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/web/vendor/lightweight-charts.standalone.production.js")
    message(FATAL_ERROR
        "The pinned Lightweight Charts bundle is missing. "
        "Run 'npm ci' followed by 'npm run sync-web-deps'.")
endif()

add_library(tradingview_chart_core
    src/domain/bar.cpp
    src/domain/bar.hpp
    src/data/csv_bar_loader.cpp
    src/data/csv_bar_loader.hpp
    src/data/demo_data_source.cpp
    src/data/demo_data_source.hpp
    src/data/market_data_parser.cpp
    src/data/market_data_parser.hpp
)
target_include_directories(tradingview_chart_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(tradingview_chart_core PUBLIC Qt6::Core)
target_compile_features(tradingview_chart_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(tradingview_chart_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(tradingview_chart_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(tradingview_chart WIN32
    src/main.cpp
    src/main_window.cpp
    src/main_window.hpp
    src/chart/chart_bridge.cpp
    src/chart/chart_bridge.hpp
    src/chart/chart_view.cpp
    src/chart/chart_view.hpp
    src/data/market_data_client.cpp
    src/data/market_data_client.hpp
    resources.qrc
)
target_link_libraries(tradingview_chart PRIVATE
    tradingview_chart_core
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    Qt6::WebChannel
    Qt6::WebEngineCore
    Qt6::WebEngineWidgets
)

if(MSVC)
    target_compile_options(tradingview_chart PRIVATE /W4 /permissive-)
else()
    target_compile_options(tradingview_chart PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TRADINGVIEW_CHART_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)

    add_executable(tradingview_chart_tests
        tests/csv_bar_loader_tests.cpp
    )
    target_link_libraries(tradingview_chart_tests PRIVATE
        tradingview_chart_core
        Qt6::Core
        Qt6::Test
    )
    add_test(NAME tradingview_chart_tests COMMAND tradingview_chart_tests)
    set_tests_properties(tradingview_chart_tests PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )

    add_test(NAME tradingview_chart_web_smoke
        COMMAND tradingview_chart --smoke-test
    )
    set_tests_properties(tradingview_chart_web_smoke PROPERTIES
        CONFIGURATIONS "Release;RelWithDebInfo;MinSizeRel"
        TIMEOUT 30
        ENVIRONMENT
            "QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu --no-sandbox"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        LABELS "integration"
    )
endif()

install(TARGETS tradingview_chart
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .
)
install(FILES
    LICENSE
    THIRD_PARTY_NOTICES.md
    assets/web/vendor/LICENSE.lightweight-charts
    assets/web/vendor/NOTICE.lightweight-charts
    DESTINATION licenses
)
