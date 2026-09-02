#include "vpl/mfxvideo.h"
#include "vpl/mfxdispatcher.h"
#include "openvino_yolo.h"

#define PRINT_INFER_TIME(id, task, t0, t1) \
    do { \
        if ((id) == 0) { \
            double ms = std::chrono::duration<double, std::milli>((t1) - (t0)).count(); \
            std::cout << "pipeline " << (id) \
                      << " task =" << (task) \
                      << " infer_time=" << ms << " ms" << std::endl; \
        } \
    } while(0)

int OpenvinoYolo::CreateYoloModel(const char *ir_path) {
    std::cout<<"Create yolo model from "<<ir_path<<std::endl;
    if (!vaContext) {
        std::cout<<__func__<<" vaContext is null. Please call SetVaDisplay firstly "<<std::endl;
        return -1;
    }

    FILE *f = fopen(ir_path, "r");
    if (!f) {
        std::cout<<__func__<<" Open IR file "<<ir_path<<" failed"<<std::endl;
        return -1;
    }
    fclose(f);

    core.set_property("GPU", { ov::cache_dir("cache_dir/yolo") });
    std::cout<<"calling loadAndPreprocessYoloModel"<<std::endl;
    yolo_model = loadAndPreprocessYoloModel(ir_path);
    std::cout<<"calling yolo ov::CompiledModel"<<std::endl;
    compiled_yolo_model = ov::CompiledModel(core.compile_model(yolo_model, *vaContext, ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)));
    std::cout<<"calling yolo ov::CompiledModel done"<<std::endl;
    getCompiledModelProperties(compiled_yolo_model);

    yoloInputNames[0] = compiled_yolo_model.input(0).get_any_name();
    yoloInputNames[1] = compiled_yolo_model.input(1).get_any_name();

    return 0;
}

int OpenvinoYolo::CreateYoloInferRequest(int id) {
    if (yolo_infer_requests.find(id) != yolo_infer_requests.end()) {
        std::cout<<__func__<<"  id "<<id<<" alreay exist!"<<std::endl;
    }
    auto request = compiled_yolo_model.create_infer_request();
    if (!request) {
        std::cout<<__func__<<" id "<<id<<" failed !"<<std::endl;
        return -1;
    }

    yolo_infer_requests[id] = request;

    return 0;
}

int OpenvinoYolo::yoloInfer(int id, int origin_pitch, int origin_height,
        int vaSurfId, LetterboxParams &params, std::vector<BoundingBox> &result, int framenum) {
    if (yolo_infer_requests.find(id) == yolo_infer_requests.end()) {
        std::cout<<__func__<<" invalid id "<<id<<std::endl;
        return -1;
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    auto &infer_request = yolo_infer_requests[id];
    auto nv12RemoteBlob = vaContext->create_tensor_nv12(INPUT_HEIGHT, INPUT_WIDTH, vaSurfId);

    infer_request.set_tensor(yoloInputNames[0], nv12RemoteBlob.first);
    infer_request.set_tensor(yoloInputNames[1], nv12RemoteBlob.second);
    infer_request.start_async();
    infer_request.wait();
    auto t1 = std::chrono::high_resolution_clock::now();
    if (verbose) PRINT_INFER_TIME(id, "YOLO", t0, t1);

    // Retrieve detection results
    auto output_tensor_det = infer_request.get_output_tensor(0);
    auto result_det = postprocess(output_tensor_det, params, origin_pitch, origin_height);
    auto boxes = result_det.first;
    auto indices = result_det.second;
    result.clear();
    for (size_t i = 0; i < indices.size(); i++) {
        int index = indices[i];
        cv::Rect box = boxes[index];
        int left = box.x;
        int top = box.y;
        int width = box.width;
        int height = box.height;
        result.push_back(BoundingBox{left, top, width, height});

        if (verbose) {
            std::cout<<"pipeline "<<id<<" index="<<index<<",left="<<left<<",top="<<top<<",width="<<width<<",height="<<height<<std::endl;
        }
    }

    return 0;
}

std::shared_ptr<ov::Model> OpenvinoYolo::loadAndPreprocessYoloModel(std::string model_path) {
    std::shared_ptr<ov::Model> model = core.read_model(model_path);
    model->reshape({{1, 3, INPUT_WIDTH, INPUT_HEIGHT}});
    std::string input_tensor_name = model->input().get_any_name();
    PrePostProcessor ppp = PrePostProcessor(model);
    InputInfo& input_info = ppp.input(input_tensor_name);
    input_info.tensor()
    .set_element_type(ov::element::u8)
    .set_color_format(ov::preprocess::ColorFormat::NV12_TWO_PLANES, {"y", "uv"})
    .set_memory_type(ov::intel_gpu::memory_type::surface)
    .set_spatial_static_shape(640, 640);

    input_info.preprocess()
        .convert_color(ov::preprocess::ColorFormat::BGR)
        .convert_element_type(ov::element::f32)
        .scale({255.0f, 255.0f, 255.0f});
    input_info.model().set_layout("NCHW");

    model = ppp.build();
    return model;
}

std::pair<std::vector<cv::Rect>, std::vector<int>> OpenvinoYolo::postprocess(const ov::Tensor &output, const LetterboxParams& params, int orig_width, int orig_height) {
    auto output_shape = output.get_shape();
    int rows = output_shape[2];
    int dimensions = output_shape[1];
    const float* data = output.data<float>();
    Mat output_buffer(output_shape[1], output_shape[2], CV_32F, const_cast<float*>(data));
    transpose(output_buffer, output_buffer);
    float score_threshold = 0.4;
    float nms_threshold = 0.7;
    std::vector<int> class_ids;
    std::vector<float> class_scores;
    std::vector<Rect> boxes;
    for (int i = 0; i < output_buffer.rows; i++) {
        Mat classes_scores = output_buffer.row(i).colRange(4, 84);
        Point class_id;
        double maxClassScore;
        minMaxLoc(classes_scores, 0, &maxClassScore, 0, &class_id);
        if (maxClassScore > score_threshold && class_id.x == 0) {
            class_scores.push_back(maxClassScore);
            class_ids.push_back(class_id.x);

            // YOLO output coordinates are relative to 640x640
            float cx = output_buffer.at<float>(i, 0);
            float cy = output_buffer.at<float>(i, 1);
            float w = output_buffer.at<float>(i, 2);
            float h = output_buffer.at<float>(i, 3);
            // Convert back to original image coordinates
            float x_center = (cx - params.pad_x) / params.scale;
            float y_center = (cy - params.pad_y) / params.scale;
            float width = w / params.scale;
            float height = h / params.scale;
            // Convert to top-left corner coordinates
            int left = static_cast<int>(x_center - width / 2);
            int top = static_cast<int>(y_center - height / 2);
            int box_width = static_cast<int>(width);
            int box_height = static_cast<int>(height);

            // Clamp coordinates to image boundaries
            left = std::max(0, std::min(left, params.origin_width - 1));
            top = std::max(0, std::min(top, params.origin_height - 1));
            box_width = std::min(box_width, params.origin_width - left);
            box_height = std::min(box_height, params.origin_height - top);

            if (box_width > 0 && box_height > 0) {
                boxes.push_back(Rect(left, top, box_width, box_height));
            }
        }
    }

    std::vector<int> indices;
    if (!boxes.empty()) {
        NMSBoxes(boxes, class_scores, score_threshold, nms_threshold, indices);
    }

    return std::make_pair(boxes, indices);
}

std::pair<std::vector<cv::Rect>, std::vector<int>> OpenvinoYolo::postprocesswithoutscale(const ov::Tensor &output) {
    auto output_shape = output.get_shape();
    const float* data = output.data<float>();
    Mat output_buffer(output_shape[1], output_shape[2], CV_32F, const_cast<float*>(data));
    transpose(output_buffer, output_buffer);
    float score_threshold = 0.4;
    float nms_threshold = 0.7;
    std::vector<float> class_scores;
    std::vector<Rect> boxes;
    for (int i = 0; i < output_buffer.rows; i++) {
        Mat classes_scores = output_buffer.row(i).colRange(4, 84);
        Point class_id;
        double maxClassScore;
        minMaxLoc(classes_scores, 0, &maxClassScore, 0, &class_id);
        if (maxClassScore > score_threshold && class_id.x == 0) {
            class_scores.push_back(maxClassScore);

            float cx = output_buffer.at<float>(i, 0);
            float cy = output_buffer.at<float>(i, 1);
            float w = output_buffer.at<float>(i, 2);
            float h = output_buffer.at<float>(i, 3);

            int left = static_cast<int>(cx - w / 2);
            int top = static_cast<int>(cy - h / 2);
            int box_width = static_cast<int>(w);
            int box_height = static_cast<int>(h);

            left = std::max(0, std::min(left, INPUT_WIDTH - 1));
            top = std::max(0, std::min(top, INPUT_HEIGHT - 1));
            box_width = std::min(box_width, INPUT_WIDTH - left);
            box_height = std::min(box_height, INPUT_HEIGHT - top);

            if (box_width > 0 && box_height > 0) {
                boxes.push_back(Rect(left, top, box_width, box_height));
            }
        }
    }

    std::vector<int> indices;
    if (!boxes.empty()) {
        NMSBoxes(boxes, class_scores, score_threshold, nms_threshold, indices);
    }

    return std::make_pair(boxes, indices);
}
