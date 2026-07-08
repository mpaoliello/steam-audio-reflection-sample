#pragma once

#include <vector>
#include <string>

struct Vec3
{
    float x;
    float y;
    float z;
};

struct PlacementPosition
{
    std::string name;      // e.g. "hihat", "kickin", "trumpet"
    int index;             // index inside that placement array
    int channelIndex;
    std::string instanceId;
    Vec3 position;
};


bool loadPlacementPositions(
    const char* jsonPath,
    std::vector<PlacementPosition>& positions
);