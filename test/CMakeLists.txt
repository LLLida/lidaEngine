project(test)

add_executable(core_test1 core_test1.c)

target_link_libraries(core_test1 PRIVATE lidaEngine)

add_test(NAME core_test1 COMMAND core_test1)
