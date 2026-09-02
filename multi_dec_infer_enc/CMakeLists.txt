cmake_minimum_required(VERSION 3.12)
project(multi_dec_infer_enc)

set(TARGET "multi_dec_infer_enc")
set(SOURCES "multi_dec_infer_enc.cpp"   "vpldecenc.cpp" "surface_crop.cpp"  "openvino_base.cpp" "openvino_yolo.cpp" "openvino_fastreid.cpp" "featurelibrary.cpp" "jpeg_encoder.cpp" "ocl_draw/opencl_filter.cpp" "ocl_draw/opencl_filter_va.cpp")

find_package(VPL REQUIRED)

find_package(OpenCL)
find_package(OpenVINO COMPONENTS Runtime)
find_package(OpenCV REQUIRED)


add_compile_definitions(RTSP_ENABLE)
add_subdirectory(rtsp)

add_executable(${TARGET} ${SOURCES})


target_include_directories(${TARGET} PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    "/opt/intel/media/include")

target_link_libraries(${TARGET} VPL::dispatcher BitstreamReader)
target_link_libraries(${TARGET}
    openvino::runtime
    va
    va-drm
    ${OpenCL_LIBRARY}
    ${OpenCV_LIBS})

