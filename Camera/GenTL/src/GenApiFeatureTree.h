#pragma once

#include <GenApi/GenApi.h>
#include <GenApi/ICategory.h>

#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace camera::gentl::detail {

// GenApi's node map also contains implementation nodes (registers, ports,
// enum entries). Only nodes reachable from the mandatory Root category are
// public device features.
inline void appendPublicFeatureNodes(
    GenApi::INode* node,
    std::vector<GenApi::INode*>& nodes,
    std::unordered_set<GenApi::INode*>& visitedCategories,
    std::unordered_set<GenApi::INode*>& visitedFeatures) {
    if (!node) return;

    if (auto* category = dynamic_cast<GenApi::ICategory*>(node)) {
        if (!visitedCategories.insert(node).second) return;

        GenApi::FeatureList_t children;
        category->GetFeatures(children);
        for (auto* child : children) {
            if (child) appendPublicFeatureNodes(
                child->GetNode(), nodes, visitedCategories, visitedFeatures);
        }
        return;
    }

    if (visitedFeatures.insert(node).second)
        nodes.push_back(node);
}

inline std::vector<GenApi::INode*> rootFeatureNodes(
    GenApi::CNodeMapRef& nodeMap) {
    auto* rootNode = nodeMap._GetNode(GenICam::gcstring("Root"));
    auto* root = dynamic_cast<GenApi::ICategory*>(rootNode);
    if (!root) {
        throw std::runtime_error(
            "GenApi Root category is missing or has an incompatible interface.");
    }

    std::vector<GenApi::INode*> nodes;
    std::unordered_set<GenApi::INode*> visitedCategories;
    std::unordered_set<GenApi::INode*> visitedFeatures;
    appendPublicFeatureNodes(rootNode, nodes, visitedCategories, visitedFeatures);
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
