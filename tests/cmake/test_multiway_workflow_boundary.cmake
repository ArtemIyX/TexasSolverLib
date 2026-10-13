if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" cmake_text)
file(READ "${SOURCE_DIR}/examples/multiway_workflow_main.cpp" source_text)

string(FIND "${cmake_text}" "foreach(_multiway_workflow train buckets inspect evaluate finalize)" target_position)
if(target_position EQUAL -1)
    message(FATAL_ERROR "missing complete multiway research target generator")
endif()

foreach(option "--help" "--config" "--seed")
    string(FIND "${source_text}" "${option}" option_position)
    if(option_position EQUAL -1)
        message(FATAL_ERROR "workflow front end does not support ${option}")
    endif()
endforeach()

string(FIND "${source_text}" "--threads <integer>" threads_option_position)
if(threads_option_position EQUAL -1)
    message(FATAL_ERROR "bucket workflow front end does not expose --threads")
endif()

foreach(startup_log "texas_multiway_buckets: starting bucket generation"
        "bucket generation goal:" "effective_threads=")
    string(FIND "${source_text}" "${startup_log}" startup_log_position)
    if(startup_log_position EQUAL -1)
        message(FATAL_ERROR "bucket workflow is missing startup log field: ${startup_log}")
    endif()
endforeach()

if(source_text MATCHES "install\\(")
    message(FATAL_ERROR "multiway workflow front end must remain non-installed")
endif()

string(FIND "${source_text}"
    "if (!checkpoint_dir.empty()) std::filesystem::create_directories(checkpoint_dir);"
    checkpoint_directory_position)
string(FIND "${source_text}" "MultiwayBucketArtifactWriter> writer;" writer_position)
if(checkpoint_directory_position EQUAL -1 OR writer_position EQUAL -1 OR
   checkpoint_directory_position GREATER writer_position)
    message(FATAL_ERROR "bucket workflow must create checkpoint directory before writer setup")
endif()
