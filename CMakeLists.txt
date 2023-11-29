cmake_minimum_required(VERSION 3.15)

project(TWEAK_SHADER)

set(CMAKE_CXX_STANDARD 11)

# Set the path to the Adobe After Effects SDK
set(AE_SDK_PATH "../../")

# Specify your source files
file(GLOB_RECURSE SOURCE_FILES 
  ${AE_SDK_PATH}/Util/*.cpp
  ./src/tweak_shader.cpp
  ./src/misc_util.cpp
)

set(AESDK_INCLUDE
	${AE_SDK_PATH}/Headers
	${AE_SDK_PATH}/Headers/SP
	${AE_SDK_PATH}/Util
	${AE_SDK_PATH}/Resources
  ./include
)

include_directories(
  ${AESDK_INCLUDE}
  ./
)

add_subdirectory(tweak_shader_cxx)

include_directories(
  tweak_shader_cxx/target/cxxbridge/rust
  tweak_shader_cxx/target/cxxbridge/tweak_shader_cxx
)

#run this to build the include dirs
add_custom_target(
    proxy_run_build 
    COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR}/tweak_shader_cxx && cargo build
)

# Specify the plugin target
add_library(${PROJECT_NAME} MODULE ${SOURCE_FILES})

add_dependencies(${PROJECT_NAME} proxy_run_build)

target_link_libraries(${PROJECT_NAME} tweak_shader_cxx)

# If we are generating an xcode project 
if(CMAKE_GENERATOR STREQUAL "Xcode")
  target_compile_options(${PROJECT_NAME} PUBLIC "-Wno-pragma-pack")
	set( CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum MacOs Version" )
	set( CMAKE_OSX_ARCHITECTURES "arm64;x86_64" )

    target_link_libraries(${PROJECT_NAME} "-framework Cocoa -framework CoreFoundation" "-framework Metal" "-framework QuartzCore")

    set_target_properties(${PROJECT_NAME} PROPERTIES
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
    )

    find_program( AFX_REZ rez /Developer/Tools )

    add_custom_command(
  		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/TweakShaderPiPL.rsrc
  		COMMAND ${AFX_REZ}
  		ARGS ${CMAKE_CURRENT_SOURCE_DIR}/TweakShaderPiPL.r
  			-o ${CMAKE_CURRENT_BINARY_DIR}/TweakShaderPiPL.rsrc
  			-useDF
			${PIPL_INCLUDES}
      	-i ../${AE_SDK_PATH}/Headers
      	-i ../${AE_SDK_PATH}/Headers/SP
      	-i ../${AE_SDK_PATH}/Util
      	-i ../${AE_SDK_PATH}/Resources
  			-D __MACH__
  	)

	set_source_files_properties(
		${CMAKE_CURRENT_BINARY_DIR}/TweakShaderPiPL.rsrc
		PROPERTIES
		MACOSX_PACKAGE_LOCATION Resources/
	)
    
  target_sources( ${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/TweakShaderPiPL.rsrc )

	set_target_properties( ${PROJECT_NAME} PROPERTIES PREFIX "" )
	set_target_properties( ${PROJECT_NAME} PROPERTIES SUFFIX "" )
	string( TIMESTAMP BUILD_YEAR "%Y" )
	set_target_properties( ${PROJECT_NAME} PROPERTIES 
    BUNDLE True
    MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
    BUNDLE_EXTENSION "plugin"
    MACOSX_BUNDLE_INFO_STRING "${PROJECT_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.adobe.AfterEffects.${PROJECT_NAME}"
    MACOSX_BUNDLE_COPYRIGHT "MIT LICENSED ${BUILD_YEAR}."
  )

	# PkgInfo
	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/PkgInfo
		COMMAND echo "eFKTFXTC" >> ${CMAKE_CURRENT_BINARY_DIR}/PkgInfo
	)

	set_source_files_properties(
		${CMAKE_CURRENT_BINARY_DIR}/PkgInfo
		PROPERTIES
		MACOSX_PACKAGE_LOCATION ""
	)

	target_sources( ${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/PkgInfo )


endif()

# For Visual Studio project generation
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	add_definitions(
		-D_CRT_SECURE_NO_WARNINGS
	)

	set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".aex")
	get_filename_component(
		AFX_REZ
		../../Resources/PiPLtool.exe
		ABSOLUTE
		CACHE
	)

	# Adobe is disgusting so we have to compile a resource format from the 90's into our project
	add_custom_command(
			OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rr
			COMMAND
			cl 
			   /I \"../${AE_SDK_PATH}/Headers\" 
			   /I \"../${AE_SDK_PATH}/Headers/SP\" 
			   /I \"../${AE_SDK_PATH}/Util\" 
			   /I \"../${AE_SDK_PATH}/Resources\" 
         /EP \"${CMAKE_CURRENT_SOURCE_DIR}/TweakShaderPiPL.r\" > ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rr
	)

	add_custom_command(
		DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rr
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rrc
		COMMAND ${AFX_REZ} ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rr ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rrc
	)

	add_custom_command(
		DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rrc
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rc
		COMMAND cl /D "MSWindows" /EP ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rrc > ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rc
	)

	target_sources(
		${PROJECT_NAME} PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rc
	)

endif()
