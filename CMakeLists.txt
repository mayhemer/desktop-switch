cmake_minimum_required(VERSION 3.20)
project(DesktopSwitch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# --- Build VirtualDesktopAccessor.dll via Cargo (both configs) ---

set(VDA_DIR "${CMAKE_SOURCE_DIR}/VirtualDesktopAccessor")
set(VDA_DLL_RELEASE "${VDA_DIR}/target/release/VirtualDesktopAccessor.dll")
set(VDA_DLL_DEBUG "${VDA_DIR}/target/debug/VirtualDesktopAccessor.dll")

add_custom_command(
    OUTPUT "${VDA_DLL_RELEASE}"
    COMMAND cargo build --release -p dll
    WORKING_DIRECTORY "${VDA_DIR}"
    COMMENT "Building VirtualDesktopAccessor.dll (Release)"
    VERBATIM
)

add_custom_command(
    OUTPUT "${VDA_DLL_DEBUG}"
    COMMAND cargo build -p dll
    WORKING_DIRECTORY "${VDA_DIR}"
    COMMENT "Building VirtualDesktopAccessor.dll (Debug)"
    VERBATIM
)

add_custom_target(VirtualDesktopAccessor_Release DEPENDS "${VDA_DLL_RELEASE}")
add_custom_target(VirtualDesktopAccessor_Debug DEPENDS "${VDA_DLL_DEBUG}")

# --- Build DesktopSwitch.exe ---

add_executable(DesktopSwitch WIN32 main.cpp)
target_link_libraries(DesktopSwitch PRIVATE shell32 user32 gdi32)

# --- Copy correct DLL variant next to exe after build ---

add_custom_command(TARGET DesktopSwitch POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${VDA_DLL_DEBUG},${VDA_DLL_RELEASE}>"
        "$<TARGET_FILE_DIR:DesktopSwitch>/VirtualDesktopAccessor.dll"
    COMMENT "Copying VirtualDesktopAccessor.dll to output directory"
)

# Build the matching Rust DLL before linking
add_custom_command(TARGET DesktopSwitch PRE_BUILD
    COMMAND cargo build $<IF:$<CONFIG:Debug>,,--release> -p dll
    WORKING_DIRECTORY "${VDA_DIR}"
    COMMENT "Ensuring VirtualDesktopAccessor.dll is up to date"
    VERBATIM
)
