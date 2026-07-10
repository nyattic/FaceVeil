#include <onnxruntime_cxx_api.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{
    std::string shapeToString(const std::vector<int64_t> &shape)
    {
        std::string result = "[";
        for (size_t i = 0; i < shape.size(); ++i)
        {
            if (i > 0)
            {
                result += ", ";
            }
            result += std::to_string(shape[i]);
        }
        result += "]";
        return result;
    }
}

int main(int argc, char *argv[])
{
    auto logger = std::make_shared<spdlog::logger>(
        "inspect", std::make_shared<spdlog::sinks::stdout_sink_mt>());
    logger->set_pattern("%v");
    spdlog::set_default_logger(logger);

    if (argc != 2)
    {
        spdlog::error("Usage: redactly_inspect_model <model.onnx>");
        return 2;
    }

    try
    {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "RedactlyInspect");
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        const std::filesystem::path modelPath = argv[1];
        Ort::Session session(env, modelPath.c_str(), options);
        Ort::AllocatorWithDefaultOptions allocator;

        spdlog::info("Inputs");
        for (size_t i = 0; i < session.GetInputCount(); ++i)
        {
            auto name = session.GetInputNameAllocated(i, allocator);
            const auto typeInfo = session.GetInputTypeInfo(i);
            const auto info = typeInfo.GetTensorTypeAndShapeInfo();
            spdlog::info("  {} type={} {}", name.get(), static_cast<int>(info.GetElementType()),
                         shapeToString(info.GetShape()));
        }

        spdlog::info("Outputs");
        for (size_t i = 0; i < session.GetOutputCount(); ++i)
        {
            auto name = session.GetOutputNameAllocated(i, allocator);
            const auto typeInfo = session.GetOutputTypeInfo(i);
            const auto info = typeInfo.GetTensorTypeAndShapeInfo();
            spdlog::info("  {} type={} {}", name.get(), static_cast<int>(info.GetElementType()),
                         shapeToString(info.GetShape()));
        }
    }
    catch (const std::exception &exception)
    {
        spdlog::error("Error: {}", exception.what());
        return 1;
    }

    return 0;
}
