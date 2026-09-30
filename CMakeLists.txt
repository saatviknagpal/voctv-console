cmake_minimum_required(VERSION 3.21)
project(VoctvConsole VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Charts DataVisualization Test)
qt_standard_project_setup()

add_library(voctv_core STATIC
    src/core/Types.h src/core/Types.cpp
    src/core/TissueModel.h src/core/TissueModel.cpp
    src/device/IDevice.h
    src/device/SimulatedVoctvDevice.h src/device/SimulatedVoctvDevice.cpp
)
target_include_directories(voctv_core PUBLIC src)
target_link_libraries(voctv_core PUBLIC Qt6::Core Qt6::Gui)

enable_testing()
function(voctv_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE voctv_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()
voctv_test(tst_types)
voctv_test(tst_tissuemodel)
voctv_test(tst_device)
