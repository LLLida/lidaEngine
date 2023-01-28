project(test)

function (make_test name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} PRIVATE lidaEngine)
  add_test(NAME ${name} COMMAND ${name})
endfunction ()

make_test(core_test1)
make_test(ecs_test2)
