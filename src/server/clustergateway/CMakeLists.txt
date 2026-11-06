#
# This file is part of Project SkyFire https://www.projectskyfire.org.
# See LICENSE.md file for Copyright information
#

file(GLOB sources_Localdir *.cpp *.h)
list(APPEND sources_Localdir
  ${CMAKE_CURRENT_SOURCE_DIR}/ClusterAdminHttp.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ClusterAdminHttp.h
)
list(REMOVE_DUPLICATES sources_Localdir)

if (USE_COREPCH)
  set(clustergateway_PCH_HDR PrecompiledHeaders/gatewayPCH.h)
  set(clustergateway_PCH_SRC PrecompiledHeaders/gatewayPCH.cpp)
endif()

set(clustergateway_SRCS
  ${sources_Localdir}
)

if( WIN32 )
  set(clustergateway_SRCS
    ${clustergateway_SRCS}
    ${sources_windows_Debugging}
  )
endif()

include_directories(
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/src/server/shared
  ${CMAKE_SOURCE_DIR}/src/server/shared/Auth
  ${CMAKE_SOURCE_DIR}/src/server/shared/Cluster
  ${CMAKE_SOURCE_DIR}/src/server/shared/Configuration
  ${CMAKE_SOURCE_DIR}/src/server/shared/Cryptography
  ${CMAKE_SOURCE_DIR}/src/server/shared/Cryptography/Authentication
  ${CMAKE_SOURCE_DIR}/src/server/shared/Database
  ${CMAKE_SOURCE_DIR}/src/server/shared/Debugging
  ${CMAKE_SOURCE_DIR}/src/server/shared/Logging
  ${CMAKE_SOURCE_DIR}/src/server/shared/Network
  ${CMAKE_SOURCE_DIR}/src/server/shared/Packets
  ${CMAKE_SOURCE_DIR}/src/server/shared/Threading
  ${CMAKE_SOURCE_DIR}/src/server/shared/Utilities
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/PrecompiledHeaders
  ${Boost_INCLUDE_DIRS}
  ${MYSQL_INCLUDE_DIR}
  ${OPENSSL_INCLUDE_DIR}
)

add_executable(clustergateway
  ${clustergateway_SRCS}
  ${clustergateway_PCH_SRC}
)

add_dependencies(clustergateway revision.h)

if( NOT WIN32 )
  set_target_properties(clustergateway PROPERTIES
    COMPILE_DEFINITIONS _SKYFIRE_GATEWAY_CONFIG="${CONF_DIR}/clustergateway.conf"
  )
endif()

target_link_libraries(clustergateway
  shared
  ${MYSQL_LIBRARY}
  ${OPENSSL_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
)

if(TARGET Boost::headers)
  target_link_libraries(clustergateway Boost::headers)
elseif(TARGET Boost::boost)
  target_link_libraries(clustergateway Boost::boost)
endif()

if( UNIX )
  install(TARGETS clustergateway DESTINATION bin)
  install(FILES clustergateway.conf.dist DESTINATION ${CONF_DIR})
elseif( WIN32 )
  install(TARGETS clustergateway RUNTIME DESTINATION ".")
  install(FILES clustergateway.conf.dist DESTINATION ".")
endif()

if (USE_COREPCH)
  add_cxx_pch(clustergateway ${clustergateway_PCH_HDR} ${clustergateway_PCH_SRC})
else()
  target_compile_options(clustergateway PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/PrecompiledHeaders/gatewayPCH.h>"
  )
endif()
