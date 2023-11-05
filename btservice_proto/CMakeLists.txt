set (btservice_proto_VERSION_MAJOR 1)
set (btservice_proto_VERSION_MINOR 0)
set (btservice_proto_VERSION_PATCH 0)

include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})

file(GLOB_RECURSE proto_files ${CMAKE_CURRENT_SOURCE_DIR}/*.proto)
protobuf_generate_cpp(proto_sources proto_headers ${proto_files})
add_library(btservice_proto SHARED ${proto_headers} ${proto_sources})
target_link_libraries(btservice_proto ${PROTOBUF_LIBRARIES})

set (btservice_proto_VERSION_STRING ${btservice_proto_VERSION_MAJOR}.${btservice_proto_VERSION_MINOR}.${btservice_proto_VERSION_PATCH})
set_target_properties(btservice_proto PROPERTIES VERSION ${btservice_proto_VERSION_STRING}
                                          SOVERSION ${btservice_proto_VERSION_MAJOR})

install(TARGETS btservice_proto DESTINATION lib)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} DESTINATION include
        FILES_MATCHING PATTERN *.h
        PATTERN CMakeFiles EXCLUDE)
