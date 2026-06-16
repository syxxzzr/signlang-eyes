#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "iceoryx2-cxx::static-lib-cxx" for configuration "Release"
set_property(TARGET iceoryx2-cxx::static-lib-cxx APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(iceoryx2-cxx::static-lib-cxx PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libiceoryx2_cxx.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS iceoryx2-cxx::static-lib-cxx )
list(APPEND _IMPORT_CHECK_FILES_FOR_iceoryx2-cxx::static-lib-cxx "${_IMPORT_PREFIX}/lib/libiceoryx2_cxx.a" )

# Import target "iceoryx2-cxx::shared-lib-cxx" for configuration "Release"
set_property(TARGET iceoryx2-cxx::shared-lib-cxx APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(iceoryx2-cxx::shared-lib-cxx PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libiceoryx2_cxx.so"
  IMPORTED_SONAME_RELEASE "libiceoryx2_cxx.so"
  )

list(APPEND _IMPORT_CHECK_TARGETS iceoryx2-cxx::shared-lib-cxx )
list(APPEND _IMPORT_CHECK_FILES_FOR_iceoryx2-cxx::shared-lib-cxx "${_IMPORT_PREFIX}/lib/libiceoryx2_cxx.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
