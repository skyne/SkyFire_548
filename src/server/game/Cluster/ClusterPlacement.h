/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SF_CLUSTER_PLACEMENT_H
#define SF_CLUSTER_PLACEMENT_H

#include "Define.h"

enum class ClusterPlaceReason : uint8
{
    Login,
    MapChange
};

struct ClusterPlaceResult
{
    uint32 nodeId;
    uint32 layerId;
    uint32 population;
    uint32 transfers;
};

class ClusterPlacement
{
public:
    static bool Choose(uint32 characterGuid, uint32 accountId, uint32 mapId, ClusterPlaceReason reason, ClusterPlaceResult& out);
};

#endif
