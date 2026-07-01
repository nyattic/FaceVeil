#include <onnxruntime_cxx_api.h>

#include <filesystem>
#include <iostream>
#include <vector>

namespace {

void printShape(const std::vector<int64_t>& shape) {
    std::cout << "[";
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) {
            std::cout << ", ";
        }
        std::cout << shape[i];
    }
    std::cout << "]";
}

}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: faceveil_inspect_model <model.onnx>\n";
        return 2;
    }

    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "FaceVeilInspect");
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        const std::filesystem::path modelPath = argv[1];
        Ort::Session session(env, modelPath.c_str(), options);
        Ort::AllocatorWithDefaultOptions allocator;

        std::cout << "Inputs\n";
        for (size_t i = 0; i < session.GetInputCount(); ++i) {
            auto name = session.GetInputNameAllocated(i, allocator);
            const auto typeInfo = session.GetInputTypeInfo(i);
            const auto info = typeInfo.GetTensorTypeAndShapeInfo();
            std::cout << "  " << name.get() << " ";
            printShape(info.GetShape());
            std::cout << "\n";
        }

        std::cout << "Outputs\n";
        for (size_t i = 0; i < session.GetOutputCount(); ++i) {
            auto name = session.GetOutputNameAllocated(i, allocator);
            const auto typeInfo = session.GetOutputTypeInfo(i);
            const auto info = typeInfo.GetTensorTypeAndShapeInfo();
            std::cout << "  " << name.get() << " ";
            printShape(info.GetShape());
            std::cout << "\n";
        }
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << "\n";
        return 1;
    }

    return 0;
}
