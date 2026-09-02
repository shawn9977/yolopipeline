#include "vpl/mfxvideo.h"
#include "vpl/mfxdispatcher.h"
#include "openvino_fastreid.h"

#define PRINT_INFER_TIME(id, task, t0, t1) \
    do { \
        if ((id) == 0) { \
            double ms = std::chrono::duration<double, std::milli>((t1) - (t0)).count(); \
            std::cout << "pipeline " << (id) \
                      << " task =" << (task) \
                      << " infer_time=" << ms << " ms" << std::endl; \
        } \
    } while(0)

OpenvinoFastReID::OpenvinoFastReID() {
    enableFastreid = false;//(getenv("NO_FASTREID") == nullptr);
}

// Pre-defined batch buckets for multi-compiled-model approach
static const int kBatchBuckets[] = {1, 4, 8};
static const int kNumBuckets = 3;
static const int kMaxPrecompiledBatch = 8;

int OpenvinoFastReID::CreateFastReIDModel(const char *ir_path) {
    std::cout<<"Create fastreid model from "<<ir_path<<std::endl;
    fastreid_model_path_ = ir_path;

    // Read model and detect batch mode from input shape
    auto tmp_model = core.read_model(ir_path);
    auto partial_shape = tmp_model->input().get_partial_shape();
    std::cout << "FastReID model input shape: " << partial_shape << std::endl;
    if (partial_shape[0].is_dynamic()) {
        enableBatchInfer = true;
        std::cout << "Dynamic batch detected, pre-compiling batch sizes: 1,4,8" << std::endl;
    } else {
        enableBatchInfer = false;
        std::cout << "Static batch=" << partial_shape[0].get_length() << ", batch pre-compilation disabled" << std::endl;
    }

    core.set_property("GPU", { ov::cache_dir("cache_dir/fastreid") });
    ov::AnyMap config = {{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)}};
    if (enableBatchInfer) {
        // Pre-compile models for all predefined batch sizes
        for (int i = 0; i < kNumBuckets; i++) {
            int bs = kBatchBuckets[i];
            std::cout<<"calling loadAndPreprocessFastReIDModel"<<std::endl;
            auto model = loadAndPreprocessFastReIDModel(ir_path, bs);
            std::cout << "  Compiling FastReID batch_size=" << bs << " ..." << std::endl;
            compiled_models_cache_[bs] = core.compile_model(model, *vaContext, config);
            std::cout << "  Compiling FastReID batch_size=" << bs << " done" << std::endl;

            // GPU plugin strips layout from compiled model input ports.
            // set_tensors() requires N(batch) in layout, so inject it back via port rt_info.
            auto& cm = compiled_models_cache_[bs];
            for (size_t j = 0; j < cm.inputs().size(); j++) {
                const_cast<ov::RTMap&>(cm.input(j).get_rt_info())
                    [ov::LayoutAttribute::get_type_info_static()] = ov::LayoutAttribute{ov::Layout("NCWH")};
            }
        }
        // Use batch=1 as the default compiled model
        compiled_fastreid_model = compiled_models_cache_[1];
    } else {
        // Static model: compile as-is (batch=1)
        std::cout << "  Compiling FastReID static batch size" << " ..." << std::endl;
        fastreid_model = loadAndPreprocessFastReIDModel(ir_path);
        compiled_fastreid_model = ov::CompiledModel(core.compile_model(fastreid_model, *vaContext, config));
        std::cout << "  Compiling FastReID static batch size done" << std::endl;
    }

    getCompiledModelProperties(compiled_fastreid_model);
    if (compiled_fastreid_model.inputs().size() >= 2) {
        fastreidInputNames[0] = compiled_fastreid_model.input(0).get_any_name();
        fastreidInputNames[1] = compiled_fastreid_model.input(1).get_any_name();
    }

    return 0;
}

int OpenvinoFastReID::CreateFastReIDInferRequest(int id) {
    if (fastreid_infer_requests.find(id) != fastreid_infer_requests.end()) {
        std::cout<<__func__<<"  id "<<id<<" alreay exist!"<<std::endl;
    }

    auto request = compiled_fastreid_model.create_infer_request();
    if (!request) {
        std::cout<<__func__<<" id "<<id<<" failed !"<<std::endl;
        return -1;
    }

    fastreid_infer_requests[id] = request;
    return 0;
}

int OpenvinoFastReID::fastReIDInfer(int id, VASurfaceID surface, int framenum) {
    auto fastreid_it = fastreid_infer_requests.find(id);
     if (fastreid_infer_requests.find(id) == fastreid_infer_requests.end()) {
        std::cout<<__func__<<" invalid id "<<id<<std::endl;
        return -1;
    }

    auto fastreid_nv12 = vaContext->create_tensor_nv12(FASTREID_INPUT_HEIGHT, FASTREID_INPUT_WIDTH, surface);
    auto &infer_request = fastreid_it->second;
    infer_request.set_tensor(fastreidInputNames[0], fastreid_nv12.first);
    infer_request.set_tensor(fastreidInputNames[1], fastreid_nv12.second);

    auto t0 = std::chrono::high_resolution_clock::now();
    infer_request.start_async();
    infer_request.wait();
    auto t1 = std::chrono::high_resolution_clock::now();
    if(verbose) PRINT_INFER_TIME(id, "FastReID", t0, t1);

    auto output_tensor = infer_request.get_output_tensor(0);
    float* data = output_tensor.data<float>();
    if (verbose) {
        feature_lib.process_feature(data);
    }

    return 0;
}

std::shared_ptr<ov::Model> OpenvinoFastReID::loadAndPreprocessFastReIDModel(
        std::string model_path, int batch_size) {
    std::shared_ptr<ov::Model> model = core.read_model(model_path);

    // Reshape dynamic batch to exact static batch before NV12 split.
    // NV12_TWO_PLANES splits the single input into 2 parameters (y, uv), so model->reshape() would fail afterwards.
    if (batch_size > 0) {
        auto partial_shape = model->input().get_partial_shape();
        if (partial_shape[0].is_dynamic()) {
            partial_shape[0] = batch_size;
            model->reshape({{model->input().get_any_name(), partial_shape}});
        }
    }

    ov::preprocess::PrePostProcessor ppp(model);
    std::string input_tensor_name = model->input().get_any_name();
    auto &input_info = ppp.input(input_tensor_name);
    input_info.tensor()
        .set_element_type(ov::element::u8)
        .set_color_format(ov::preprocess::ColorFormat::NV12_TWO_PLANES, {"y", "uv"})
        .set_memory_type(ov::intel_gpu::memory_type::surface)
        .set_spatial_static_shape(FASTREID_INPUT_HEIGHT, FASTREID_INPUT_WIDTH);

    input_info.preprocess()
        .convert_color(ov::preprocess::ColorFormat::BGR)
        .convert_element_type(ov::element::f32);

    input_info.model().set_layout("NCWH");

    ppp.output().tensor().set_element_type(ov::element::f32);
    model = ppp.build();

    // After ppp.build(), NV12 two-plane inputs (y, uv) need layout with batch dim (N),
    // so that set_tensors() can identify the batch dimension. Must match model layout "NCWH".
    if (batch_size > 0) {
        for (auto& param : model->get_parameters()) {
            param->set_layout("NCWH");
        }
    }

    return model;
}

// Select the smallest pre-compiled batch size >= n, capped at kMaxPrecompiledBatch.
int OpenvinoFastReID::selectBatchSize(int n) {
    for (int i = 0; i < kNumBuckets; i++) {
        if (kBatchBuckets[i] >= n) return kBatchBuckets[i];
    }
    return kMaxPrecompiledBatch;
}

ov::InferRequest& OpenvinoFastReID::getOrCreateBatchInferRequest(int id, int batch_size) {
    auto &pipeline_map = batch_infer_requests_[id];
    auto it = pipeline_map.find(batch_size);
    if (it != pipeline_map.end()) {
        return it->second;
    }

    auto cache_it = compiled_models_cache_.find(batch_size);
    if (cache_it == compiled_models_cache_.end()) {
        // Should not happen with pre-compiled buckets, but fallback to batch=1
        cache_it = compiled_models_cache_.find(1);
    }
    pipeline_map[batch_size] = cache_it->second.create_infer_request();
    return pipeline_map[batch_size];
}

int OpenvinoFastReID::fastReIDBatchInfer(int id, const std::vector<VASurfaceID>& surfaces, int framenum) {
    if (surfaces.empty()) return 0;

    int total = static_cast<int>(surfaces.size());
    int processed = 0;

    while (processed < total) {
        int remaining = total - processed;
        int chunk = std::min(kMaxPrecompiledBatch, remaining);
        int batch = selectBatchSize(chunk);

        auto &infer_request = getOrCreateBatchInferRequest(id, batch);

        // Build tensor vectors, padded to bucket batch size.
        // Pad by repeating the last real surface in this chunk.
        std::vector<ov::Tensor> y_tensors;
        std::vector<ov::Tensor> uv_tensors;
        y_tensors.reserve(batch);
        uv_tensors.reserve(batch);

        for (int i = 0; i < batch; i++) {
            int surf_idx = processed + std::min(i, chunk - 1);
            auto nv12 = vaContext->create_tensor_nv12(
                    FASTREID_INPUT_HEIGHT, FASTREID_INPUT_WIDTH, surfaces[surf_idx]);
            y_tensors.push_back(nv12.first);
            uv_tensors.push_back(nv12.second);
        }

        auto &compiled_model = compiled_models_cache_[batch];
        infer_request.set_tensors(compiled_model.input(0), y_tensors);
        infer_request.set_tensors(compiled_model.input(1), uv_tensors);

        auto t0 = std::chrono::high_resolution_clock::now();
        infer_request.infer();
        auto t1 = std::chrono::high_resolution_clock::now();
        if (verbose) PRINT_INFER_TIME(id, "FastReID_batch", t0, t1);

        // Only process the first 'chunk' outputs (ignore padding)
        auto output_tensor = infer_request.get_output_tensor(0);
        float* data = output_tensor.data<float>();
        if (verbose) {
            for (int i = 0; i < chunk; i++) {
                feature_lib.process_feature(data + i * FeatureLibrary::FEATURE_DIM);
            }
        }

        processed += chunk;
    }

    return 0;
}
