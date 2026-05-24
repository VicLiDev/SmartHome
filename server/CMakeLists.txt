# cmake needs this line
CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

# Define project name
PROJECT(SmartHome)

# cross compile
# SET(CMAKE_C_COMPILER "/usr/bin/arm-linux-gnueabihf-gcc")
# SET(CMAKE_CXX_COMPILER "/usr/bin/arm-linux-gnueabihf-g++")

INCLUDE_DIRECTORIES(include
	modules/videocap/include
	modules/videocap/onvif
	modules/videocap/onvifcomm
	/usr/local/include)

LINK_DIRECTORIES(/usr/local/lib)

#=========================================== OpenCV ============================================
# Find OpenCV, you may need to set OpenCV_DIR variable
# to the absolute path to the directory containing OpenCVConfig.cmake file
# via the command line or GUI
FIND_PACKAGE(OpenCV REQUIRED)
IF(NOT OpenCV_FOUND)
	MESSAGE(FATAL_ERROR "OpenCV Not Found!")
ENDIF(NOT OpenCV_FOUND)

# If the package has been found, several variables will
# be set, you can find the full list with descriptions
# in the OpenCVConfig.cmake file.
# Print some message showing some of them
message(STATUS "OpenCV library status:")
message(STATUS "    version: ${OpenCV_VERSION}")
message(STATUS "    libraries: ${OpenCV_LIBS}")
message(STATUS "    include path: ${OpenCV_INCLUDE_DIRS}")
#========================================= OpenCV end ==========================================

#========================================= ffmpeg ==============================================
SET(LIBS 
	avdevice m xcb xcb-shm xcb-shape xcb-xfixes asound SDL2 sndio Xv X11 Xext avfilter pthread swscale postproc avformat z avcodec lzma mp3lame theoraenc theoradec ogg vorbis vorbisenc x264 xvidcore swresample avutil 
	ssl crypto)
#========================================= ffmpeg end ==========================================

# Add subdirectory
ADD_SUBDIRECTORY(modules/videocap)
ADD_SUBDIRECTORY(src)

#========================================= Smart out ===========================================
# Declare the executable target built from your sources
# AUX_SOURCE_DIRECTORY(. SMARTHOME_SRC)

# Declare the executable target built from sources
# ADD_EXECUTABLE(SmartHome ${SMARTHOME_SRC})

# Link application with OpenCV libraries
# TARGET_LINK_LIBRARIES(SmartHome ${OpenCV_LIBS})