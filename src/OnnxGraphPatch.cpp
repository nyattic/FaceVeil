#include "cloakframe/OnnxGraphPatch.hpp"

#include <array>
#include <cstring>
#include <set>
#include <string>
#include <string_view>

namespace cloakframe
{
    namespace
    {
        constexpr std::uint32_t kWireVarint = 0;
        constexpr std::uint32_t kWireFixed64 = 1;
        constexpr std::uint32_t kWireLengthDelimited = 2;
        constexpr std::uint32_t kWireFixed32 = 5;

        constexpr std::string_view kScalesInitializerName = "cloakframe_upsample_scales";

        struct PbField
        {
            std::uint32_t number = 0;
            std::uint32_t wireType = 0;
            std::uint64_t varint = 0;
            std::vector<std::uint8_t> bytes;
        };

        using PbMessage = std::vector<PbField>;

        std::optional<PbMessage> parseMessage(const std::uint8_t *data, std::size_t size)
        {
            PbMessage fields;
            std::size_t pos = 0;
            const auto readVarint = [&](std::uint64_t &value)
            {
                value = 0;
                for (int shift = 0; shift < 64 && pos < size; shift += 7)
                {
                    const std::uint8_t byte = data[pos++];
                    value |= static_cast<std::uint64_t>(byte & 0x7FU) << shift;
                    if ((byte & 0x80U) == 0)
                    {
                        return true;
                    }
                }
                return false;
            };

            while (pos < size)
            {
                std::uint64_t key = 0;
                if (!readVarint(key))
                {
                    return std::nullopt;
                }
                PbField field;
                field.number = static_cast<std::uint32_t>(key >> 3);
                field.wireType = static_cast<std::uint32_t>(key & 7U);
                if (field.number == 0)
                {
                    return std::nullopt;
                }
                switch (field.wireType)
                {
                    case kWireVarint:
                        if (!readVarint(field.varint))
                        {
                            return std::nullopt;
                        }
                        break;
                    case kWireFixed64:
                    case kWireFixed32:
                    {
                        const std::size_t width = field.wireType == kWireFixed64 ? 8 : 4;
                        if (size - pos < width)
                        {
                            return std::nullopt;
                        }
                        field.bytes.assign(data + pos, data + pos + width);
                        pos += width;
                        break;
                    }
                    case kWireLengthDelimited:
                    {
                        std::uint64_t length = 0;
                        if (!readVarint(length) || length > size - pos)
                        {
                            return std::nullopt;
                        }
                        field.bytes.assign(data + pos, data + pos + length);
                        pos += length;
                        break;
                    }
                    default:
                        return std::nullopt;
                }
                fields.push_back(std::move(field));
            }
            return fields;
        }

        std::optional<PbMessage> parseMessage(const std::vector<std::uint8_t> &bytes)
        {
            return parseMessage(bytes.data(), bytes.size());
        }

        void appendVarint(std::vector<std::uint8_t> &out, std::uint64_t value)
        {
            while (value >= 0x80U)
            {
                out.push_back(static_cast<std::uint8_t>((value & 0x7FU) | 0x80U));
                value >>= 7;
            }
            out.push_back(static_cast<std::uint8_t>(value));
        }

        std::vector<std::uint8_t> serializeMessage(const PbMessage &fields)
        {
            std::vector<std::uint8_t> out;
            for (const auto &field: fields)
            {
                appendVarint(out, (static_cast<std::uint64_t>(field.number) << 3) | field.wireType);
                if (field.wireType == kWireVarint)
                {
                    appendVarint(out, field.varint);
                }
                else if (field.wireType == kWireLengthDelimited)
                {
                    appendVarint(out, field.bytes.size());
                    out.insert(out.end(), field.bytes.begin(), field.bytes.end());
                }
                else
                {
                    out.insert(out.end(), field.bytes.begin(), field.bytes.end());
                }
            }
            return out;
        }

        PbField makeStringField(std::uint32_t number, std::string_view value)
        {
            PbField field;
            field.number = number;
            field.wireType = kWireLengthDelimited;
            field.bytes.assign(value.begin(), value.end());
            return field;
        }

        PbField makeVarintField(std::uint32_t number, std::uint64_t value)
        {
            PbField field;
            field.number = number;
            field.wireType = kWireVarint;
            field.varint = value;
            return field;
        }

        PbField *findFirst(PbMessage &fields, std::uint32_t number, std::uint32_t wireType)
        {
            for (auto &field: fields)
            {
                if (field.number == number && field.wireType == wireType)
                {
                    return &field;
                }
            }
            return nullptr;
        }

        std::string fieldString(const PbField &field)
        {
            return {field.bytes.begin(), field.bytes.end()};
        }

        bool rewriteNested(PbField &parent, std::uint32_t number,
                           const auto &transform)
        {
            auto parsed = parseMessage(parent.bytes);
            if (!parsed)
            {
                return false;
            }
            auto *child = findFirst(*parsed, number, kWireLengthDelimited);
            if (child == nullptr || !transform(*child))
            {
                return false;
            }
            parent.bytes = serializeMessage(*parsed);
            return true;
        }

        bool setDimensionValue(PbField &dimension, int size)
        {
            const auto parsed = parseMessage(dimension.bytes);
            if (!parsed)
            {
                return false;
            }
            dimension.bytes = serializeMessage(
                {makeVarintField(1, static_cast<std::uint64_t>(size))});
            return true;
        }

        bool setInputSpatialDims(PbField &input, int size)
        {
            return rewriteNested(input, 2, [size](PbField &type)
            {
                return rewriteNested(type, 1, [size](PbField &tensor)
                {
                    return rewriteNested(tensor, 2, [size](PbField &shape)
                    {
                        auto parsed = parseMessage(shape.bytes);
                        if (!parsed)
                        {
                            return false;
                        }
                        std::vector<PbField *> dims;
                        for (auto &field: *parsed)
                        {
                            if (field.number == 1 && field.wireType == kWireLengthDelimited)
                            {
                                dims.push_back(&field);
                            }
                        }
                        if (dims.size() != 4)
                        {
                            return false;
                        }
                        if (!setDimensionValue(*dims[2], size)
                            || !setDimensionValue(*dims[3], size))
                        {
                            return false;
                        }
                        shape.bytes = serializeMessage(*parsed);
                        return true;
                    });
                });
            });
        }

        bool makeOutputShapeUnknown(PbField &output)
        {
            return rewriteNested(output, 2, [](PbField &type)
            {
                return rewriteNested(type, 1, [](PbField &tensor)
                {
                    auto parsed = parseMessage(tensor.bytes);
                    if (!parsed)
                    {
                        return false;
                    }
                    auto *shape = findFirst(*parsed, 2, kWireLengthDelimited);
                    if (shape == nullptr)
                    {
                        return true;
                    }
                    auto shapeFields = parseMessage(shape->bytes);
                    if (!shapeFields)
                    {
                        return false;
                    }
                    for (auto &dimension: *shapeFields)
                    {
                        if (dimension.number == 1 && dimension.wireType == kWireLengthDelimited)
                        {
                            dimension.bytes.clear();
                        }
                    }
                    shape->bytes = serializeMessage(*shapeFields);
                    tensor.bytes = serializeMessage(*parsed);
                    return true;
                });
            });
        }

        bool resizeModeIsSupported(const PbMessage &node)
        {
            for (const auto &field: node)
            {
                if (field.number != 5 || field.wireType != kWireLengthDelimited)
                {
                    continue;
                }
                const auto attribute = parseMessage(field.bytes);
                if (!attribute)
                {
                    return false;
                }
                std::string name;
                std::string text;
                for (const auto &entry: *attribute)
                {
                    if (entry.number == 1 && entry.wireType == kWireLengthDelimited)
                    {
                        name = fieldString(entry);
                    }
                    else if (entry.number == 4 && entry.wireType == kWireLengthDelimited)
                    {
                        text = fieldString(entry);
                    }
                }
                if (name == "mode" && text != "nearest")
                {
                    return false;
                }
                if (name == "coordinate_transformation_mode" && text != "asymmetric")
                {
                    return false;
                }
            }
            return true;
        }

        enum class ResizePatch
        {
            NotResize,
            Patched,
            Unsupported,
        };

        ResizePatch patchResizeNode(PbField &nodeField,
                                    const std::set<std::string> &initializerNames,
                                    std::set<std::string> &detachedInitializers)
        {
            auto node = parseMessage(nodeField.bytes);
            if (!node)
            {
                return ResizePatch::Unsupported;
            }
            const auto *opType = findFirst(*node, 4, kWireLengthDelimited);
            if (opType == nullptr || fieldString(*opType) != "Resize")
            {
                return ResizePatch::NotResize;
            }

            std::vector<std::size_t> inputIndices;
            for (std::size_t i = 0; i < node->size(); ++i)
            {
                if ((*node)[i].number == 1 && (*node)[i].wireType == kWireLengthDelimited)
                {
                    inputIndices.push_back(i);
                }
            }
            if (inputIndices.size() != 4)
            {
                return ResizePatch::NotResize;
            }
            if (!initializerNames.contains(fieldString((*node)[inputIndices[3]])))
            {
                return ResizePatch::NotResize;
            }
            if (!resizeModeIsSupported(*node))
            {
                return ResizePatch::Unsupported;
            }

            detachedInitializers.insert(fieldString((*node)[inputIndices[3]]));
            (*node)[inputIndices[2]] = makeStringField(1, kScalesInitializerName);
            node->erase(node->begin() + static_cast<std::ptrdiff_t>(inputIndices[3]));
            nodeField.bytes = serializeMessage(*node);
            return ResizePatch::Patched;
        }

        PbField makeScalesInitializer()
        {
            PbMessage tensor;
            tensor.push_back(makeVarintField(1, 4));
            tensor.push_back(makeVarintField(2, 1));
            tensor.push_back(makeStringField(8, kScalesInitializerName));

            const std::array<float, 4> scales = {1.0F, 1.0F, 2.0F, 2.0F};
            PbField raw;
            raw.number = 9;
            raw.wireType = kWireLengthDelimited;
            raw.bytes.resize(sizeof(scales));
            std::memcpy(raw.bytes.data(), scales.data(), sizeof(scales));
            tensor.push_back(std::move(raw));

            PbField field;
            field.number = 5;
            field.wireType = kWireLengthDelimited;
            field.bytes = serializeMessage(tensor);
            return field;
        }
    }

    std::optional<std::vector<std::uint8_t>>
    makeOnnxSpatialDimsFixed(const std::vector<std::uint8_t> &modelBytes, int size)
    {
        if (size <= 0)
        {
            return std::nullopt;
        }
        auto model = parseMessage(modelBytes);
        if (!model)
        {
            return std::nullopt;
        }
        auto *graphField = findFirst(*model, 7, kWireLengthDelimited);
        if (graphField == nullptr)
        {
            return std::nullopt;
        }
        auto graph = parseMessage(graphField->bytes);
        if (!graph)
        {
            return std::nullopt;
        }

        std::set<std::string> initializerNames;
        for (auto &field: *graph)
        {
            if (field.number != 5 || field.wireType != kWireLengthDelimited)
            {
                continue;
            }
            auto initializer = parseMessage(field.bytes);
            if (!initializer)
            {
                return std::nullopt;
            }
            if (const auto *name = findFirst(*initializer, 8, kWireLengthDelimited))
            {
                initializerNames.insert(fieldString(*name));
            }
        }
        if (initializerNames.contains(std::string(kScalesInitializerName)))
        {
            return std::nullopt;
        }

        std::vector<PbField *> inputs;
        for (auto &field: *graph)
        {
            if (field.number == 11 && field.wireType == kWireLengthDelimited)
            {
                inputs.push_back(&field);
            }
        }
        if (inputs.size() != 1 || !setInputSpatialDims(*inputs.front(), size))
        {
            return std::nullopt;
        }

        std::set<std::string> detachedInitializers;
        bool patchedResize = false;
        for (auto &field: *graph)
        {
            if (field.number == 1 && field.wireType == kWireLengthDelimited)
            {
                switch (patchResizeNode(field, initializerNames, detachedInitializers))
                {
                    case ResizePatch::Unsupported:
                        return std::nullopt;
                    case ResizePatch::Patched:
                        patchedResize = true;
                        break;
                    case ResizePatch::NotResize:
                        break;
                }
            }
        }

        for (auto &field: *graph)
        {
            if (field.number == 12 && field.wireType == kWireLengthDelimited)
            {
                if (!makeOutputShapeUnknown(field))
                {
                    return std::nullopt;
                }
            }
        }

        std::erase_if(*graph, [](const PbField &field)
        {
            return field.number == 13;
        });

        if (!detachedInitializers.empty())
        {
            std::set<std::string> referencedInputs;
            for (const auto &field: *graph)
            {
                if (field.number != 1 || field.wireType != kWireLengthDelimited)
                {
                    continue;
                }
                const auto node = parseMessage(field.bytes);
                if (!node)
                {
                    continue;
                }
                for (const auto &entry: *node)
                {
                    if (entry.number == 1 && entry.wireType == kWireLengthDelimited)
                    {
                        referencedInputs.insert(fieldString(entry));
                    }
                }
            }
            std::erase_if(*graph, [&](const PbField &field)
            {
                if (field.number != 5 || field.wireType != kWireLengthDelimited)
                {
                    return false;
                }
                auto initializer = parseMessage(field.bytes);
                if (!initializer)
                {
                    return false;
                }
                const auto *name = findFirst(*initializer, 8, kWireLengthDelimited);
                if (name == nullptr)
                {
                    return false;
                }
                const std::string text = fieldString(*name);
                return detachedInitializers.contains(text) && !referencedInputs.contains(text);
            });
        }

        if (patchedResize)
        {
            graph->push_back(makeScalesInitializer());
        }
        graphField->bytes = serializeMessage(*graph);
        return serializeMessage(*model);
    }
}
