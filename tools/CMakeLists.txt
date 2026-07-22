add_executable(cloakframe_inspect_model
    inspect_model.cpp
)

target_link_libraries(cloakframe_inspect_model PRIVATE onnxruntime::onnxruntime spdlog::spdlog)
cloakframe_harden(cloakframe_inspect_model)
