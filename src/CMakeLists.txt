add_library(mc_core STATIC
    core/version.cpp
)
add_library(mc::core ALIAS mc_core)

target_include_directories(mc_core
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

mc_apply_warnings(mc_core)
