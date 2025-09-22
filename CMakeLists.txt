cmake_minimum_required(VERSION 3.28)
project(jowi_asio VERSION 1.0.0)
set (CMAKE_CXX_STANDARD 23)
set (CMAKE_CXX_STANDARD_REQUIRED true)
set (CMAKE_CXX_SCAN_FOR_MODULES true)

option (JOWI_ASIO_BUILD_TESTS "Build Tests" OFF)

if (NOT TARGET jowi::crogger)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/libs/jowi-cli)
endif()

add_library (${PROJECT_NAME})
add_library (jowi::asio ALIAS ${PROJECT_NAME})
target_sources (${PROJECT_NAME}
  PUBLIC
    FILE_SET CXX_MODULES 
    FILES
      ${CMAKE_CURRENT_LIST_DIR}/src/awaitable.cc
      ${CMAKE_CURRENT_LIST_DIR}/src/awaitables.cc
      ${CMAKE_CURRENT_LIST_DIR}/src/lf_deque.cc
      ${CMAKE_CURRENT_LIST_DIR}/src/main.cc
      ${CMAKE_CURRENT_LIST_DIR}/src/shared_ptr.cc
      ${CMAKE_CURRENT_LIST_DIR}/src/task.cc
)
target_link_libraries(jowi_asio PRIVATE jowi::crogger)
target_compile_features (${PROJECT_NAME} PUBLIC cxx_std_23)

if (JOWI_ASIO_BUILD_TESTS) 
  include(CTest)
  if (NOT TARGET moderna::test_lib)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/libs/test-lib)
  endif()
  jowi_add_test(test_jowi_asio_task
    ${CMAKE_CURRENT_LIST_DIR}/tests/asio.cc
    LIBRARIES ${PROJECT_NAME}
    SANITIZERS all
  )
endif()

if (JOWI_INSTALL)
  include(GNUInstallDirs)
  set (JOWI_COMPONENT_NAME "asio")
  set_property (
    TARGET ${PROJECT_NAME}
    PROPERTY EXPORT_NAME ${JOWI_COMPONENT_NAME}
  )
  install (
    TARGETS ${PROJECT_NAME}
    EXPORT jowi
    FILE_SET
      CXX_MODULES
      DESTINATION "${CMAKE_INSTALL_PREFIX}/cxx_src/jowi/${JOWI_COMPONENT_NAME}"
    CXX_MODULES_BMI
      COMPONENT ${JOWI_COMPONENT_NAME}
      DESTINATION "${CMAKE_INSTALL_PREFIX}/cxx_modules/jowi/${JOWI_COMPONENT_NAME}"
  )
  if (PROJECT_IS_TOP_LEVEL)
    install (
      EXPORT jowi
      DESTINATION "${CMAKE_INSTALL_LIBDIR}/jowi"
      FILE jowi-config.cmake
      NAMESPACE jowi::
      CXX_MODULES_DIRECTORY cxx_modules/jowi
    )
  endif()
endif()