cmake_minimum_required(VERSION 3.10)
project(NetSpeedCtrl C)

set(CMAKE_C_STANDARD 99)

# Specify executable and source files
add_executable(NetSpeedCtrl WIN32
    src/main.c
    src/network.c
    src/utils.c
    src/resource.rc
)

# Target Include Directories
target_include_directories(NetSpeedCtrl PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/WinDivert/include
)

# Libraries
# Assuming 64-bit build
target_link_directories(NetSpeedCtrl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/WinDivert/x64)
target_link_libraries(NetSpeedCtrl PRIVATE WinDivert comctl32 user32 gdi32 advapi32 shlwapi winmm)

# Copy DLL and SYS to output dir after build
add_custom_command(TARGET NetSpeedCtrl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/WinDivert/x64/WinDivert.dll"
        $<TARGET_FILE_DIR:NetSpeedCtrl>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/WinDivert/x64/WinDivert64.sys"
        $<TARGET_FILE_DIR:NetSpeedCtrl>
)
