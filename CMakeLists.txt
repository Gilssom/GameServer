cmake_minimum_required(VERSION 3.24)
project(GameServerPortfolio LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "This IOCP portfolio targets Windows with MSVC.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_library(portfolio_common STATIC src/auth/AuthStore.cpp)
target_include_directories(portfolio_common PUBLIC src)
target_compile_definitions(portfolio_common PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(portfolio_common PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(portfolio_common PUBLIC bcrypt odbc32)

add_executable(PortfolioServer src/server/main.cpp src/server/IocpServer.cpp)
target_link_libraries(PortfolioServer PRIVATE portfolio_common ws2_32)
target_compile_definitions(PortfolioServer PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(PortfolioServer PRIVATE /W4 /permissive- /utf-8)

add_executable(DummyClient src/client/main.cpp)
target_link_libraries(DummyClient PRIVATE portfolio_common ws2_32)
target_compile_definitions(DummyClient PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(DummyClient PRIVATE /W4 /permissive- /utf-8)
