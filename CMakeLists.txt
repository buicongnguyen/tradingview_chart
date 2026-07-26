cmake_minimum_required(VERSION 3.24)

project(TradingViewChart VERSION 0.5.0 LANGUAGES CXX)

option(
    TRADINGVIEW_CHART_BUILD_TESTS
    "Build the unit test suite (desktop host builds only)"
    ON
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Sql)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/web/vendor/lightweight-charts.standalone.production.js")
    message(FATAL_ERROR
        "The pinned Lightweight Charts bundle is missing. "
        "Run 'npm ci' followed by 'npm run sync-web-deps'.")
endif()

add_library(tradingview_chart_core
    src/analysis/technical_indicators.cpp
    src/analysis/technical_indicators.hpp
    src/chart/chart_bridge.cpp
    src/chart/chart_bridge.hpp
    src/domain/bar.cpp
    src/domain/bar.hpp
    src/data/csv_bar_loader.cpp
    src/data/csv_bar_loader.hpp
    src/data/demo_data_source.cpp
    src/data/demo_data_source.hpp
    src/data/historical_data_store.cpp
    src/data/historical_data_store.hpp
    src/data/market_data_parser.cpp
    src/data/market_data_parser.hpp
    src/research/alpha_vantage_research_parser.cpp
    src/research/alpha_vantage_research_parser.hpp
    src/research/margin_risk.cpp
    src/research/margin_risk.hpp
    src/research/research_models.cpp
    src/research/research_models.hpp
    src/strategy/replay_session.cpp
    src/strategy/replay_session.hpp
    src/strategy/strategy_engine.cpp
    src/strategy/strategy_engine.hpp
    src/strategy/strategy_models.cpp
    src/strategy/strategy_models.hpp
    src/util/csv_security.cpp
    src/util/csv_security.hpp
    src/watchlists/watchlist_workspace.cpp
    src/watchlists/watchlist_workspace.hpp
)
target_include_directories(tradingview_chart_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(tradingview_chart_core PUBLIC Qt6::Core Qt6::Sql)
target_compile_features(tradingview_chart_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(tradingview_chart_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(tradingview_chart_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ANDROID)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Qml Quick QuickControls2 WebView)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0002 NEW)
    qt_policy(SET QTP0004 NEW)

    qt_add_executable(tradingview_chart
        src/android_main.cpp
        src/data/android_http_client.cpp
        src/data/android_http_client.hpp
        src/data/market_data_client.cpp
        src/data/market_data_client.hpp
        src/mobile/mobile_controller.cpp
        src/mobile/mobile_controller.hpp
    )
    set_source_files_properties(
        qml/Main.qml
        PROPERTIES QT_RESOURCE_ALIAS Main.qml
    )
    qt_add_qml_module(tradingview_chart
        URI TradingViewChart
        VERSION 1.0
        QML_FILES
            qml/Main.qml
    )
    target_link_libraries(tradingview_chart PRIVATE
        tradingview_chart_core
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::WebView
    )

    # Android's native WebView can load package assets directly. Copy the
    # canonical chart renderer into a generated package overlay so the desktop
    # and mobile applications cannot silently drift to different libraries.
    set(android_package_dir "${CMAKE_CURRENT_BINARY_DIR}/android-package")
    file(MAKE_DIRECTORY "${android_package_dir}/assets/web/vendor")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/android/AndroidManifest.xml"
        "${android_package_dir}/AndroidManifest.xml"
        COPYONLY
    )
    file(
        COPY "${CMAKE_CURRENT_SOURCE_DIR}/android/res"
        DESTINATION "${android_package_dir}"
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/android/assets/web/mobile.html"
        "${android_package_dir}/assets/web/mobile.html"
        COPYONLY
    )
    foreach(web_asset IN ITEMS
        chart.js
        styles.css
        vendor/lightweight-charts.standalone.production.js
    )
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/web/${web_asset}"
            "${android_package_dir}/assets/web/${web_asset}"
            COPYONLY
        )
    endforeach()
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/android/src/com/buicongnguyen/tradingviewchart/AndroidHttpClient.java"
        "${android_package_dir}/src/com/buicongnguyen/tradingviewchart/AndroidHttpClient.java"
        COPYONLY
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/android/src/com/buicongnguyen/tradingviewchart/AndroidUiMetrics.java"
        "${android_package_dir}/src/com/buicongnguyen/tradingviewchart/AndroidUiMetrics.java"
        COPYONLY
    )
    file(MAKE_DIRECTORY "${android_package_dir}/assets/licenses")
    foreach(license_file IN ITEMS
        LICENSE
        THIRD_PARTY_NOTICES.md
        assets/web/vendor/LICENSE.lightweight-charts
        assets/web/vendor/NOTICE.lightweight-charts
    )
        get_filename_component(license_name "${license_file}" NAME)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/${license_file}"
            "${android_package_dir}/assets/licenses/${license_name}"
            COPYONLY
        )
    endforeach()

    set_target_properties(tradingview_chart PROPERTIES
        QT_ANDROID_PACKAGE_NAME "com.buicongnguyen.tradingviewchart"
        QT_ANDROID_PACKAGE_SOURCE_DIR "${android_package_dir}"
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 36
        QT_ANDROID_VERSION_CODE 500
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
    )

else()
    find_package(Qt6 6.8 REQUIRED COMPONENTS
        Widgets
        WebChannel
        WebEngineCore
        WebEngineWidgets
    )

    qt_add_executable(tradingview_chart WIN32
        src/main.cpp
        src/main_window.cpp
        src/main_window.hpp
        src/chart/chart_view.cpp
        src/chart/chart_view.hpp
        src/data/market_data_client.cpp
        src/data/market_data_client.hpp
        src/network/bounded_network_reply.cpp
        src/network/bounded_network_reply.hpp
        src/research/alpha_vantage_research_client.cpp
        src/research/alpha_vantage_research_client.hpp
        src/strategy/strategy_lab_widget.cpp
        src/strategy/strategy_lab_widget.hpp
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
endif()

if(MSVC)
    target_compile_options(tradingview_chart PRIVATE /W4 /permissive-)
else()
    target_compile_options(tradingview_chart PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TRADINGVIEW_CHART_BUILD_TESTS AND NOT ANDROID)
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

    add_executable(tradingview_chart_indicator_tests
        tests/technical_indicators_tests.cpp
    )
    target_link_libraries(tradingview_chart_indicator_tests PRIVATE
        tradingview_chart_core
        Qt6::Core
        Qt6::Test
    )
    add_test(
        NAME tradingview_chart_indicator_tests
        COMMAND tradingview_chart_indicator_tests
    )

    add_executable(tradingview_chart_watchlist_tests
        tests/watchlist_workspace_tests.cpp
    )
    target_link_libraries(tradingview_chart_watchlist_tests PRIVATE
        tradingview_chart_core
        Qt6::Core
        Qt6::Test
    )
    add_test(
        NAME tradingview_chart_watchlist_tests
        COMMAND tradingview_chart_watchlist_tests
    )

    add_executable(tradingview_chart_research_tests
        tests/research_models_tests.cpp
    )
    target_link_libraries(tradingview_chart_research_tests PRIVATE
        tradingview_chart_core
        Qt6::Core
        Qt6::Test
    )
    add_test(
        NAME tradingview_chart_research_tests
        COMMAND tradingview_chart_research_tests
    )

    add_executable(tradingview_chart_history_tests
        tests/historical_data_store_tests.cpp
    )
    target_link_libraries(tradingview_chart_history_tests PRIVATE
        tradingview_chart_core
        Qt6::Core
        Qt6::Sql
        Qt6::Test
    )
    add_test(
        NAME tradingview_chart_history_tests
        COMMAND tradingview_chart_history_tests
    )

    add_executable(tradingview_chart_strategy_tests
        tests/strategy_engine_tests.cpp
    )
    target_link_libraries(tradingview_chart_strategy_tests PRIVATE
        tradingview_chart_core
        Qt6::Core
        Qt6::Test
    )
    add_test(
        NAME tradingview_chart_strategy_tests
        COMMAND tradingview_chart_strategy_tests
    )

    set_tests_properties(
        tradingview_chart_tests
        tradingview_chart_indicator_tests
        tradingview_chart_watchlist_tests
        tradingview_chart_research_tests
        tradingview_chart_history_tests
        tradingview_chart_strategy_tests
        PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::QSQLiteDriverPlugin>/.."
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

if(NOT ANDROID)
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
endif()
