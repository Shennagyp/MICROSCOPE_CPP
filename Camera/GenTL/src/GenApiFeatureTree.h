#pragma once

#include <GenApi/GenApi.h>
#include <GenApi/ICategory.h>

#include <stdexcept>
#include <string_view>
#include <vector>

namespace camera::gentl::detail {

// GenApi's node map also contains implementation nodes (registers, ports,
// enum entries). Only nodes reachable from the mandatory Root category are
// public device features.
inline std::vector<GenApi::INode*> rootFeatureNodes(
    GenApi::CNodeMapRef& nodeMap) {
    auto* rootNode = nodeMap._GetNode(GenICam::gcstring("Root"));
    auto* root = dynamic_cast<GenApi::ICategory*>(rootNode);
    if (!root) {
        throw std::runtime_error(
            "GenApi Root category is missing or has an incompatible interface.");
    }

    GenApi::FeatureList_t values;
    root->GetFeatures(values);

    std::vector<GenApi::INode*> nodes;
    nodes.reserve(values.size());
    for (auto* value : values) {
        if (value) {
            if (auto* node = value->GetNode()) nodes.push_back(node);
        }
    }
    return nodes;
}

inline GenApi::INode* findRootFeatureNode(
    GenApi::CNodeMapRef& nodeMap, std::string_view name) {
    for (auto* node : rootFeatureNodes(nodeMap)) {
        if (node && name == node->GetName().c_str()) return node;
    }
    return nullptr;
}

} // namespace camera::gentl::detail
