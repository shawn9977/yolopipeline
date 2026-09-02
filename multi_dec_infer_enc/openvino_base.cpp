#include "openvino_base.h"

OpenvinoInferBase::OpenvinoInferBase() {
    if (getenv("VERBOSE")) {
        verbose = true;
    }
}

OpenvinoInferBase::~OpenvinoInferBase() {
    if (vaContext) {
        delete vaContext;
        vaContext = nullptr;
    }
}

void OpenvinoInferBase::SetVaDisplay(VADisplay dpy) {
    if (!vaContext) {
        vadpy = dpy;
        vaContext = new ov::intel_gpu::ocl::VAContext(core, vadpy);
    }
    else {
        std::cout<<__func__<<" VADisplay only need to set once "<<std::endl;
    }
}

void OpenvinoInferBase::getCompiledModelProperties(ov::CompiledModel& compiled_model) {
    auto supported_properties = compiled_model.get_property(ov::supported_properties);
    std::cout << "Compiled Model Properties:" << std::endl;
    for (const auto& prop_name : supported_properties) {
        try {
            auto value = compiled_model.get_property(prop_name);
            std::cout << "  " << prop_name << ": " << value.as<std::string>() << std::endl;
        } catch (const ov::Exception& e) {
            continue;
        }
    }
}
