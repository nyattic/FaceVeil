#include "cloakframe/OrtAcceleration.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace cloakframe
{
    namespace
    {
        bool providerAvailable(const char *providerName)
        {
            const auto providers = Ort::GetAvailableProviders();
            return std::ranges::find(providers, std::string(providerName)) != providers.end();
        }
    }

    const char *ortAcceleratorName(OrtAccelerator accelerator)
    {
        switch (accelerator)
        {
            case OrtAccelerator::CoreML:
                return "CoreML";
            case OrtAccelerator::DirectML:
                return "DirectML";
            case OrtAccelerator::CUDA:
                return "CUDA";
            case OrtAccelerator::MIGraphX:
                return "MIGraphX";
            case OrtAccelerator::ROCm:
                return "ROCm";
            case OrtAccelerator::None:
                break;
        }
        return "CPU";
    }

    OrtAccelerator applyOrtAcceleration(Ort::SessionOptions &options, bool enabled)
    {
        if (!enabled)
        {
            return OrtAccelerator::None;
        }

#if defined(__APPLE__)
        if (providerAvailable("CoreMLExecutionProvider"))
        {
            try
            {
                const std::unordered_map<std::string, std::string> coremlOptions = {
                    {"ModelFormat", "MLProgram"},
                    {"MLComputeUnits", "ALL"},
                };
                options.AppendExecutionProvider("CoreML", coremlOptions);
                return OrtAccelerator::CoreML;
            }
            catch (const Ort::Exception &error)
            {
                spdlog::warn("CoreML acceleration unavailable, falling back to CPU: {}", error.what());
            }
        }
        else
        {
            spdlog::info("CoreML execution provider not present in this ONNX Runtime build; using CPU.");
        }
#elif defined(_WIN32)
        if (providerAvailable("DmlExecutionProvider"))
        {
            try
            {
                options.DisableMemPattern();
                options.SetExecutionMode(ORT_SEQUENTIAL);
                options.AppendExecutionProvider("DML", std::unordered_map<std::string, std::string>{});
                return OrtAccelerator::DirectML;
            }
            catch (const Ort::Exception &error)
            {
                options.EnableMemPattern();
                spdlog::warn("DirectML acceleration unavailable, falling back to CPU: {}", error.what());
            }
        }
        else
        {
            spdlog::info("DirectML execution provider not present in this ONNX Runtime build; using CPU.");
        }
#elif defined(__linux__)
        if (providerAvailable("CUDAExecutionProvider"))
        {
            try
            {
                OrtCUDAProviderOptions cudaOptions;
                options.AppendExecutionProvider_CUDA(cudaOptions);
                return OrtAccelerator::CUDA;
            }
            catch (const Ort::Exception &error)
            {
                spdlog::warn("CUDA acceleration unavailable: {}", error.what());
            }
        }

        if (providerAvailable("MIGraphXExecutionProvider"))
        {
            try
            {
                OrtMIGraphXProviderOptions migraphxOptions{};
                migraphxOptions.migraphx_mem_limit = SIZE_MAX;
                options.AppendExecutionProvider_MIGraphX(migraphxOptions);
                return OrtAccelerator::MIGraphX;
            }
            catch (const Ort::Exception &error)
            {
                spdlog::warn("MIGraphX acceleration unavailable: {}", error.what());
            }
        }

        if (providerAvailable("ROCMExecutionProvider"))
        {
            try
            {
                OrtROCMProviderOptions rocmOptions;
                options.AppendExecutionProvider_ROCM(rocmOptions);
                return OrtAccelerator::ROCm;
            }
            catch (const Ort::Exception &error)
            {
                spdlog::warn("ROCm acceleration unavailable: {}", error.what());
            }
        }

        spdlog::info("No supported Linux GPU execution provider is available; using CPU.");
#endif
        return OrtAccelerator::None;
    }
}
