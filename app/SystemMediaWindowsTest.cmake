if(WIN32)
    add_executable(system-media-windows-test EXCLUDE_FROM_ALL
        ../tools/system_media_windows_test.cpp
        player/SystemMediaBackend_win.cpp
        player/SystemMediaWindowsSmoke.h)
    target_include_directories(system-media-windows-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(system-media-windows-test PRIVATE Qt6::Core runtimeobject ole32 uuid)
    # Protect optimized COM ABI calls even in an otherwise Debug test build.
    if(MINGW)
        target_compile_options(system-media-windows-test PRIVATE -O3)
    endif()
    add_test(NAME system-media-windows COMMAND system-media-windows-test)
    set_tests_properties(system-media-windows PROPERTIES LABELS "requires-gui" TIMEOUT 30)
endif()
