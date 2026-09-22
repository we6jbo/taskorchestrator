cmake_minimum_required(VERSION 3.21)
project(taskorchestrator VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core)

qt_add_executable(taskorchestrator
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    orchestrator.cpp
    orchestrator.h
)

target_link_libraries(taskorchestrator PRIVATE Qt6::Widgets Qt6::Core)

install(TARGETS taskorchestrator RUNTIME DESTINATION bin)
install(FILES tg_context_snapshot.json DESTINATION share/taskorchestrator)
