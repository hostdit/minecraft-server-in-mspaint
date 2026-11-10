add_executable(mcpaint_loader main.cpp)
target_compile_definitions(mcpaint_loader PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(mcpaint_loader PRIVATE advapi32)
