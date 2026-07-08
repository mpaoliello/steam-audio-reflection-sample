#include "Placements.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <ranges>
#include <functional>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

bool loadPlacementPositions(
    const char* jsonPath,
    std::vector<PlacementPosition>& positions
)
{
    positions.clear();

    std::ifstream file(jsonPath);
    if (!file.is_open())
    {
        std::cerr << "Failed to open JSON file: " << jsonPath << std::endl;
        return false;
    }

    json root;

    try
    {
        file >> root;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to parse JSON: " << e.what() << std::endl;
        return false;
    }

    if (!root.contains("placements") || !root["placements"].is_object())
    {
        std::cerr << "JSON does not contain a valid 'placements' object." << std::endl;
        return false;
    }

    const json& placements = root["placements"];

    for (auto it = placements.begin(); it != placements.end(); ++it)
    {
        const std::string placementName = it.key();
        const json& placementArray = it.value();

        if (!placementArray.is_array())
            continue;

        for (std::size_t i = 0; i < placementArray.size(); ++i)
        {
            const json& elem = placementArray[i];

            if (!elem.contains("transform") ||
                !elem["transform"].contains("position"))
            {
                continue;
            }

            const json& pos = elem["transform"]["position"];

            if (!pos.contains("x") ||
                !pos.contains("y") ||
                !pos.contains("z"))
            {
                continue;
            }

            PlacementPosition p;

            p.name = placementName;
            p.index = static_cast<int>(i);
            p.channelIndex = elem.value("channelIndex", -1);
            p.instanceId = elem.value("instanceId", "");

            p.position.x = pos["x"].get<float>();
            p.position.y = pos["y"].get<float>();
            p.position.z = pos["z"].get<float>();

            positions.push_back(p);
        }
    }

    std::ranges::stable_sort(
        positions,
        std::less{},
        &PlacementPosition::channelIndex
    );

    return true;
}