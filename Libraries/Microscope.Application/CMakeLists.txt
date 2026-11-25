add_library(microscope_application STATIC
    src/RuntimeHandle.cpp)
add_library(Microscope::Application ALIAS microscope_application)

target_compile_features(microscope_application PUBLIC cxx_std_17)
target_include_directories(microscope_application
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

target_link_libraries(microscope_application
    PUBLIC Microscope::Core)

set_target_properties(microscope_application PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    VERSION "0.9.0"
    SOVERSION "0")

if(BUILD_TESTING)
    add_executable(microscope_application_contract_tests
        tests/ApplicationContractTests.cpp)
    target_link_libraries(microscope_application_contract_tests
        PRIVATE Microscope::Application)
    add_test(
        NAME microscope_application_contract_tests
        COMMAND microscope_application_contract_tests)
endif()
