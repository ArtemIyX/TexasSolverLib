if (NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" project_cmake)
foreach(category IN ITEMS
    TEXASSOLVER_PLATFORM_SOURCES
    TEXASSOLVER_TEST_SUPPORT_SOURCES
    TEXASSOLVER_LEGACY_RESEARCH_SOURCES
    TEXASSOLVER_FIXED_RESEARCH_SOURCES
    TEXASSOLVER_FLAT_MCCFR_RESEARCH_SOURCES
    TEXASSOLVER_STABLE_INTERNAL_HEADERS
    TEXASSOLVER_COMPATIBILITY_HEADERS
    TEXASSOLVER_RESEARCH_HEADERS)
    if (NOT project_cmake MATCHES "set\\(${category}")
        message(FATAL_ERROR "missing source-classification category: ${category}")
    endif()
endforeach()
if (NOT project_cmake MATCHES "texassolver_verify_source_classification\\(\\)")
    message(FATAL_ERROR "source classification is not enforced during configuration")
endif()
