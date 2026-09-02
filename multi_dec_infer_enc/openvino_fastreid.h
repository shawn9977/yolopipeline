#pragma once

#include "openvino_base.h"
#include "featurelibrary.h"

#define FASTREID_INPUT_WIDTH 128
#define FASTREID_INPUT_HEIGHT 384

class OpenvinoFastReID : virtual public OpenvinoInferBase {
public:
    OpenvinoFastReID();
    ~OpenvinoFastReID() override = default;

    int CreateFastReIDModel(const char *ir_path);
    int CreateFastReIDInferRequest(int id);
    int fastReIDInfer(int id, VASurfaceID surface, int framenum);
    int fastReIDBatchInfer(int id, const std::vector<VASurfaceID>& surfaces, int framenum);

    std::shared_ptr<ov::Model> loadAndPreprocessFastReIDModel(std::string model_path, int batch_size = 0);

    std::shared_ptr<ov::Model> fastreid_model;
    ov::CompiledModel compiled_fastreid_model;
    std::unordered_map<int, ov::InferRequest> fastreid_infer_requests;
    std::string fastreidInputNames[2];

    FeatureLibrary feature_lib;
    bool enableFastreid = false;
    bool enableBatchInfer = false;

private:
    std::string fastreid_model_path_;
    std::unordered_map<int, ov::CompiledModel> compiled_models_cache_;
    std::unordered_map<int, std::unordered_map<int, ov::InferRequest>> batch_infer_requests_;

    static int selectBatchSize(int n);
    ov::InferRequest& getOrCreateBatchInferRequest(int id, int batch_size);
};
