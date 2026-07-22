#pragma once

#include <onnxruntime_cxx_api.h>

namespace cloakframe
{
    enum class OrtAccelerator
    {
        None,
        CoreML,
        DirectML,
        CUDA,
        MIGraphX,
        ROCm,
    };

    [[nodiscard]] const char *ortAcceleratorName(OrtAccelerator accelerator);

    OrtAccelerator applyOrtAcceleration(Ort::SessionOptions &options, bool enabled);
}
