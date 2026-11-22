if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Bridge32Host must be built as 32-bit: cmake -A Win32")
endif()

add_executable(Bridge32Host WIN32 main.cpp)
target_compile_features(Bridge32Host PRIVATE cxx_std_17)
target_compile_definitions(Bridge32Host PRIVATE NOMINMAX UNICODE _UNICODE)
target_link_libraries(Bridge32Host PRIVATE shell32 user32)

# static CRT so the user does not need the 32-bit VC++ redistributable
if(MSVC)
    set_property(TARGET Bridge32Host PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
