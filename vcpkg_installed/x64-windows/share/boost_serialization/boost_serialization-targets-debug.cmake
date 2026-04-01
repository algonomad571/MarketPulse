#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Boost::serialization" for configuration "Debug"
set_property(TARGET Boost::serialization APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Boost::serialization PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/debug/lib/boost_serialization-vc142-mt-gd-x64-1_90.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/bin/boost_serialization-vc142-mt-gd-x64-1_90.dll"
  )

list(APPEND _cmake_import_check_targets Boost::serialization )
list(APPEND _cmake_import_check_files_for_Boost::serialization "${_IMPORT_PREFIX}/debug/lib/boost_serialization-vc142-mt-gd-x64-1_90.lib" "${_IMPORT_PREFIX}/debug/bin/boost_serialization-vc142-mt-gd-x64-1_90.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
