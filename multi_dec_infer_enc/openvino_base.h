#pragma once

#include "opencv2/opencv.hpp"
#include "openvino/openvino.hpp"

#include <openvino/runtime/intel_gpu/ocl/va.hpp>
#include <openvino/runtime/intel_gpu/properties.hpp>

using namespace cv;
using namespace dnn;
using namespace ov::preprocess;

struct LetterboxParams {
    float scale;
    int pad_x;
    int pad_y;
    int new_width;
    int new_height;
    int origin_width;
    int origin_height;
};

struct BoundingBox {
    int left;
    int top;
    int w;
    int h;
};

class OpenvinoInferBase {
public:
    OpenvinoInferBase();
    virtual ~OpenvinoInferBase();

    void SetVaDisplay(VADisplay dpy);
    void getCompiledModelProperties(ov::CompiledModel& compiled_model);

    VADisplay vadpy = nullptr;
    ov::Core core;
    ov::intel_gpu::ocl::VAContext* vaContext = nullptr;
    bool verbose = false;
};
