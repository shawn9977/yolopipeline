#pragma once

#include "openvino_base.h"
#define INPUT_WIDTH 640
#define INPUT_HEIGHT 640

class OpenvinoYolo : virtual public OpenvinoInferBase {
public:
    OpenvinoYolo() = default;
    ~OpenvinoYolo() override = default;

    int CreateYoloModel(const char *ir_path);
    int CreateYoloInferRequest(int id);
    int yoloInfer(int id, int origin_pitch, int origin_height,
        int vaSurfId, LetterboxParams &params, std::vector<BoundingBox> &result, int framenum);

    std::shared_ptr<ov::Model> loadAndPreprocessYoloModel(std::string model_path);
    std::pair<std::vector<cv::Rect>, std::vector<int>> postprocess(const ov::Tensor &output, const LetterboxParams& params, int orig_width, int orig_height);
    std::pair<std::vector<cv::Rect>, std::vector<int>> postprocesswithoutscale(const ov::Tensor &output);

    std::shared_ptr<ov::Model> yolo_model;
    ov::CompiledModel compiled_yolo_model;
    std::unordered_map<int, ov::InferRequest> yolo_infer_requests;
    std::unordered_map<int, cv::Mat> yolo_temp_images;
    std::string yoloInputNames[2];
};
