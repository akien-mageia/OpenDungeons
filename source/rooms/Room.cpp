/*
 *  Copyright (C) 2011-2015  OpenDungeons Team
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "rooms/Room.h"

#include "network/ODServer.h"
#include "network/ServerNotification.h"
#include "entities/Creature.h"
#include "entities/RenderedMovableEntity.h"
#include "entities/Tile.h"
#include "game/Player.h"
#include "game/Seat.h"
#include "gamemap/GameMap.h"
#include "rooms/RoomManager.h"
#include "rooms/RoomType.h"
#include "utils/ConfigManager.h"
#include "utils/Helper.h"
#include "utils/LogManager.h"

#include <istream>
#include <ostream>

Room::Room(GameMap* gameMap):
    Building(gameMap),
    mNumActiveSpots(0)
{
}

bool Room::compareTile(Tile* tile1, Tile* tile2)
{
    if(tile1->getX() < tile2->getX())
        return true;

    if(tile1->getX() == tile2->getX())
        return (tile1->getY() < tile2->getY());

    return false;
}

void Room::addToGameMap()
{
    getGameMap()->addRoom(this);
    setIsOnMap(true);
    if(!getGameMap()->isServerGameMap())
        return;

    getGameMap()->addActiveObject(this);
}

void Room::removeFromGameMap()
{
    getGameMap()->removeRoom(this);
    setIsOnMap(false);
    if(!getGameMap()->isServerGameMap())
        return;

    for(Seat* seat : getGameMap()->getSeats())
    {
        for(Tile* tile : mCoveredTiles)
            seat->notifyBuildingRemovedFromGameMap(this, tile);
        for(Tile* tile : mCoveredTilesDestroyed)
            seat->notifyBuildingRemovedFromGameMap(this, tile);
    }

    removeAllBuildingObjects();
    getGameMap()->removeActiveObject(this);
}

void Room::absorbRoom(Room *r)
{
    LogManager::getSingleton().logMessage(getGameMap()->serverStr() + "Room=" + getName() + " is absorbing room=" + r->getName());

    mCentralActiveSpotTiles.insert(mCentralActiveSpotTiles.end(), r->mCentralActiveSpotTiles.begin(), r->mCentralActiveSpotTiles.end());
    r->mCentralActiveSpotTiles.clear();
    mLeftWallsActiveSpotTiles.insert(mLeftWallsActiveSpotTiles.end(), r->mLeftWallsActiveSpotTiles.begin(), r->mLeftWallsActiveSpotTiles.end());
    r->mLeftWallsActiveSpotTiles.clear();
    mRightWallsActiveSpotTiles.insert(mRightWallsActiveSpotTiles.end(), r->mRightWallsActiveSpotTiles.begin(), r->mRightWallsActiveSpotTiles.end());
    r->mRightWallsActiveSpotTiles.clear();
    mTopWallsActiveSpotTiles.insert(mTopWallsActiveSpotTiles.end(), r->mTopWallsActiveSpotTiles.begin(), r->mTopWallsActiveSpotTiles.end());
    r->mTopWallsActiveSpotTiles.clear();
    mBottomWallsActiveSpotTiles.insert(mBottomWallsActiveSpotTiles.end(), r->mBottomWallsActiveSpotTiles.begin(), r->mBottomWallsActiveSpotTiles.end());
    r->mBottomWallsActiveSpotTiles.clear();
    mNumActiveSpots += r->mNumActiveSpots;

    // Every creature working in this room should go to the new one (this is used in the server map only)
    if(getGameMap()->isServerGameMap())
    {
        for(Creature* creature : r->mCreaturesUsingRoom)
        {
            if(creature->isJobRoom(r))
                creature->changeJobRoom(this);
            else if(creature->isEatRoom(r))
                creature->changeEatRoom(this);
            else
            {
                OD_ASSERT_TRUE_MSG(false, "creature=" + creature->getName() + ", oldRoom=" + r->getName() + ", newRoom=" + getName());
            }
        }
        mCreaturesUsingRoom.insert(mCreaturesUsingRoom.end(), r->mCreaturesUsingRoom.begin(), r->mCreaturesUsingRoom.end());
        r->mCreaturesUsingRoom.clear();
    }

    mBuildingObjects.insert(r->mBuildingObjects.begin(), r->mBuildingObjects.end());
    r->mBuildingObjects.clear();

    // We consider that the new room will be composed with the covered tiles it uses + the covered tiles absorbed. In the
    // absorbed room, we consider all tiles as destroyed. It will get removed from gamemap when enemy vision will be cleared
    for(Tile* tile : r->mCoveredTiles)
    {
        mCoveredTiles.push_back(tile);
        TileData* tileData = r->mTileData[tile];
        mTileData[tile] = tileData->cloneTileData();
        tileData->mHP = 0.0;
        tile->setCoveringBuilding(this);
    }

    r->mCoveredTilesDestroyed.insert(r->mCoveredTilesDestroyed.end(), r->mCoveredTiles.begin(), r->mCoveredTiles.end());
    r->mCoveredTiles.clear();
}

void Room::reorderRoomTiles(std::vector<Tile*>& tiles)
{
    // We try to keep the same tile disposition as if the room was created like this in the first
    // place to make sure building objects are disposed the same way
    std::sort(tiles.begin(), tiles.end(), Room::compareTile);
}

bool Room::addCreatureUsingRoom(Creature* c)
{
    if(!hasOpenCreatureSpot(c))
        return false;

    mCreaturesUsingRoom.push_back(c);
    return true;
}

void Room::removeCreatureUsingRoom(Creature *c)
{
    for (unsigned int i = 0; i < mCreaturesUsingRoom.size(); ++i)
    {
        if (mCreaturesUsingRoom[i] == c)
        {
            mCreaturesUsingRoom.erase(mCreaturesUsingRoom.begin() + i);
            break;
        }
    }
}

Creature* Room::getCreatureUsingRoom(unsigned index)
{
    if (index >= mCreaturesUsingRoom.size())
        return nullptr;

    return mCreaturesUsingRoom[index];
}

std::string Room::getRoomStreamFormat()
{
    return "typeRoom\tname\tseatId\tnumTiles\t\tSubsequent Lines: tileX\ttileY";
}

void Room::setupRoom(const std::string& name, Seat* seat, const std::vector<Tile*>& tiles)
{
    setName(name);
    setSeat(seat);
    for(Tile* tile : tiles)
    {
        mCoveredTiles.push_back(tile);
        TileData* tileData = createTileData(tile);
        mTileData[tile] = tileData;
        tileData->mHP = DEFAULT_TILE_HP;

        tile->setCoveringBuilding(this);
    }
}

void Room::checkForRoomAbsorbtion()
{
    bool isRoomAbsorbed = false;
    for (Tile* tile : getGameMap()->tilesBorderedByRegion(getCoveredTiles()))
    {
        Room* room = tile->getCoveringRoom();
        if(room == nullptr)
            continue;
        if(room == this)
            continue;
        if(room->getSeat() != getSeat())
            continue;
        if(room->getType() != getType())
            continue;

        absorbRoom(room);
        // All the tiles from the absorbed room have been transferred to this one
        // No need to delete it since it will be removed during its next upkeep
        isRoomAbsorbed = true;
    }

    if(isRoomAbsorbed)
        reorderRoomTiles(mCoveredTiles);
}

void Room::updateActiveSpots()
{
    // Active spots are handled by the server only
    if(!getGameMap()->isServerGameMap())
        return;

    std::vector<Tile*> centralActiveSpotTiles;
    std::vector<Tile*> leftWallsActiveSpotTiles;
    std::vector<Tile*> rightWallsActiveSpotTiles;
    std::vector<Tile*> topWallsActiveSpotTiles;
    std::vector<Tile*> bottomWallsActiveSpotTiles;

    // Detect the centers of 3x3 squares tiles
    for(unsigned int i = 0, size = mCoveredTiles.size(); i < size; ++i)
    {
        bool foundTop = false;
        bool foundTopLeft = false;
        bool foundTopRight = false;
        bool foundLeft = false;
        bool foundRight = false;
        bool foundBottomLeft = false;
        bool foundBottomRight = false;
        bool foundBottom = false;
        Tile* tile = mCoveredTiles[i];
        int tileX = tile->getX();
        int tileY = tile->getY();

        // Check all other tiles to know whether we have one center tile.
        for(unsigned int j = 0; j < size; ++j)
        {
            // Don't check the tile itself
            if (tile == mCoveredTiles[j])
                continue;

            // Check whether the tile around the tile checked is already a center spot
            // as we can't have two center spots next to one another.
            if (std::find(centralActiveSpotTiles.begin(), centralActiveSpotTiles.end(), mCoveredTiles[j]) != centralActiveSpotTiles.end())
                continue;

            int tile2X = mCoveredTiles[j]->getX();
            int tile2Y = mCoveredTiles[j]->getY();

            if(tile2X == tileX - 1)
            {
                if (tile2Y == tileY + 1)
                    foundTopLeft = true;
                else if (tile2Y == tileY)
                    foundLeft = true;
                else if (tile2Y == tileY - 1)
                    foundBottomLeft = true;
            }
            else if(tile2X == tileX)
            {
                if (tile2Y == tileY + 1)
                    foundTop = true;
                else if (tile2Y == tileY - 1)
                    foundBottom = true;
            }
            else if(tile2X == tileX + 1)
            {
                if (tile2Y == tileY + 1)
                    foundTopRight = true;
                else if (tile2Y == tileY)
                    foundRight = true;
                else if (tile2Y == tileY - 1)
                    foundBottomRight = true;
            }
        }

        // Check whether we found a tile at the center of others
        if (foundTop && foundTopLeft && foundTopRight && foundLeft && foundRight
                && foundBottomLeft && foundBottomRight && foundBottom)
        {
            centralActiveSpotTiles.push_back(tile);
        }
    }

    // Now that we've got the center tiles, we can test the tile around for walls.
    for (unsigned int i = 0, size = centralActiveSpotTiles.size(); i < size; ++i)
    {
        Tile* centerTile = centralActiveSpotTiles[i];
        if (centerTile == nullptr)
            continue;

        // Test for walls around
        // Up
        Tile* testTile;
        testTile = getGameMap()->getTile(centerTile->getX(), centerTile->getY() + 2);
        if (testTile != nullptr && testTile->isWallClaimedForSeat(getSeat()))
        {
            Tile* topTile = getGameMap()->getTile(centerTile->getX(), centerTile->getY() + 1);
            if (topTile != nullptr)
                topWallsActiveSpotTiles.push_back(topTile);
        }
        // Up for 4 tiles wide room
        testTile = getGameMap()->getTile(centerTile->getX(), centerTile->getY() + 3);
        if (testTile != nullptr && testTile->isWallClaimedForSeat(getSeat()))
        {
            bool isFound = true;
            for(int k = 0; k < 3; ++k)
            {
                Tile* testTile2 = getGameMap()->getTile(centerTile->getX() + k - 1, centerTile->getY() + 2);
                if((testTile2 == nullptr) || (testTile2->getCoveringBuilding() != this))
                {
                    isFound = false;
                    break;
                }
            }

            if(isFound)
            {
                Tile* topTile = getGameMap()->getTile(centerTile->getX(), centerTile->getY() + 2);
                topWallsActiveSpotTiles.push_back(topTile);
            }
        }

        // Down
        testTile = getGameMap()->getTile(centerTile->getX(), centerTile->getY() - 2);
        if (testTile != nullptr && testTile->isWallClaimedForSeat(getSeat()))
        {
            Tile* bottomTile = getGameMap()->getTile(centerTile->getX(), centerTile->getY() - 1);
            if (bottomTile != nullptr)
                bottomWallsActiveSpotTiles.push_back(bottomTile);
        }
        // Down for 4 tiles wide room
        testTile = getGameMap()->getTile(centerTile->getX(), centerTile->getY() - 3);
        if (testTile != nullptr && testTile->isWallClaimedForSeat(getSeat()))
        {
            bool isFound = true;
            for(int k = 0; k < 3; ++k)
            {
                Tile* testTile2 = getGameMap()->getTile(centerTile->getX() + k - 1, centerTile->getY() - 2);
                if((testTile2 == nullptr) || (testTile2->getCoveringBuilding() != this))
                {
                    isFound = false;
                    break;
                }
            }

            if(isFound)
            {
                Tile* topTile = getGameMap()->getTile(centerTile->getX(), centerTile->getY() - 2);
                bottomWallsActiveSpotTiles.push_back(topTile);
            }
        }

        // Left
        testTile = getGameMap()->getTile(centerTile->getX() - 2, centerTile->getY());
        if (testTile != nullptr && testTile->isWallClaimedForSeat(getSeat()))
        {
            Tile* leftTile = getGameMap()->getTile(centerTile->getX() - 1, centerTile->getY());
            if (leftTile != nullptr)
                leftWallsActiveSpotTiles.push_back(leftTile);
        }
        // Left for 4 tiles wide room
        testTile = getGameMap()->getTile(centerTile->getX() - 3, centerTile->getY());
        if (testTile != nullptr && testTile->isWallClaimedForSeat(getSeat()))
        {
            bool isFound = true;
            for(int k = 0; k < 3; ++k)
            {
                Tile* testTile2 = getGameMap()->getTile(centerTile->getX() - 2, centerTile->getY() + k - 1);
                if((testTile2 == nullptr) || (testTile2->getCoveringBuilding() != this))
                {
                    isFound = false;
                    break;
                }
            }

            if(isFound)
            {
                Tile* topTile = getGameMap()->getTile(centerTile->getX() - 2, centerTile->getY());
                leftWallsActiveSpotTiles.push_back(topTile);
            }
        }

        // Right
        testTile = getGameMap()->getTile(centerTile->getX() + 2, centerTile->getY());
        if (testTile != nullptr && testTile->isWallClaimedForSeat(getSeat()))
        {
            Tile* rightTile = getGameMap()->getTile(centerTile->getX() + 1, centerTile->getY());
            if (rightTile != nullptr)
                rightWallsActiveSpotTiles.push_back(rightTile);
        }
        // Right for 4 tiles wide room
        testTile = getGameMap()->getTile(centerTile->getX() + 3, centerTile->getY());
        if (testTile != nullptr && testTile->isWallClaimedForSeat(getSeat()))
        {
            bool isFound = true;
            for(int k = 0; k < 3; ++k)
            {
                Tile* testTile2 = getGameMap()->getTile(centerTile->getX() + 2, centerTile->getY() + k - 1);
                if((testTile2 == nullptr) || (testTile2->getCoveringBuilding() != this))
                {
                    isFound = false;
                    break;
                }
            }

            if(isFound)
            {
                Tile* topTile = getGameMap()->getTile(centerTile->getX() + 2, centerTile->getY());
                rightWallsActiveSpotTiles.push_back(topTile);
            }
        }
    }

    activeSpotCheckChange(activeSpotCenter, mCentralActiveSpotTiles, centralActiveSpotTiles);
    activeSpotCheckChange(activeSpotLeft, mLeftWallsActiveSpotTiles, leftWallsActiveSpotTiles);
    activeSpotCheckChange(activeSpotRight, mRightWallsActiveSpotTiles, rightWallsActiveSpotTiles);
    activeSpotCheckChange(activeSpotTop, mTopWallsActiveSpotTiles, topWallsActiveSpotTiles);
    activeSpotCheckChange(activeSpotBottom, mBottomWallsActiveSpotTiles, bottomWallsActiveSpotTiles);

    mCentralActiveSpotTiles = centralActiveSpotTiles;
    mLeftWallsActiveSpotTiles = leftWallsActiveSpotTiles;
    mRightWallsActiveSpotTiles = rightWallsActiveSpotTiles;
    mTopWallsActiveSpotTiles = topWallsActiveSpotTiles;
    mBottomWallsActiveSpotTiles = bottomWallsActiveSpotTiles;

    // Updates the number of active spots
    mNumActiveSpots = mCentralActiveSpotTiles.size()
                      + mLeftWallsActiveSpotTiles.size() + mRightWallsActiveSpotTiles.size()
                      + mTopWallsActiveSpotTiles.size() + mBottomWallsActiveSpotTiles.size();
}

void Room::activeSpotCheckChange(ActiveSpotPlace place, const std::vector<Tile*>& originalSpotTiles,
    const std::vector<Tile*>& newSpotTiles)
{
    // We create the non existing tiles
    for(std::vector<Tile*>::const_iterator it = newSpotTiles.begin(); it != newSpotTiles.end(); ++it)
    {
        Tile* tile = *it;
        if(std::find(originalSpotTiles.begin(), originalSpotTiles.end(), tile) == originalSpotTiles.end())
        {
            // The tile do not exist
            RenderedMovableEntity* ro = notifyActiveSpotCreated(place, tile);
            if(ro != nullptr)
            {
                // The room wants to build a room onject. We add it to the gamemap
                addBuildingObject(tile, ro);
                ro->createMesh();
            }
        }
    }
    // We remove the suppressed tiles
    for(std::vector<Tile*>::const_iterator it = originalSpotTiles.begin(); it != originalSpotTiles.end(); ++it)
    {
        Tile* tile = *it;
        if(std::find(newSpotTiles.begin(), newSpotTiles.end(), tile) == newSpotTiles.end())
        {
            // The tile has been removed
            notifyActiveSpotRemoved(place, tile);
        }
    }
}

bool Room::canBeRepaired() const
{
    switch(getType())
    {
        case RoomType::dungeonTemple:
        case RoomType::portal:
            return false;
        default:
            return true;
    }
}

RenderedMovableEntity* Room::notifyActiveSpotCreated(ActiveSpotPlace place, Tile* tile)
{
    return nullptr;
}

void Room::notifyActiveSpotRemoved(ActiveSpotPlace place, Tile* tile)
{
    removeBuildingObject(tile);
}

void Room::exportHeadersToStream(std::ostream& os) const
{
    os << getType() << "\t";
}

void Room::exportTileDataToStream(std::ostream& os, Tile* tile, TileData* tileData) const
{
    if(getGameMap()->isInEditorMode())
        return;

    os << "\t" << tileData->mHP;

    // We only save enemy seats that have vision on the building
    std::vector<Seat*> seatsToSave;
    for(Seat* seat : tileData->mSeatsVision)
    {
        if(getSeat()->isAlliedSeat(seat))
            continue;

        seatsToSave.push_back(seat);
    }
    uint32_t nbSeatsVision = seatsToSave.size();
    os << "\t" << nbSeatsVision;
    for(Seat* seat : seatsToSave)
        os << "\t" << seat->getId();
}

void Room::importTileDataFromStream(std::istream& is, Tile* tile, TileData* tileData)
{
    if(is.eof())
    {
        // Default initialization
        tileData->mHP = DEFAULT_TILE_HP;
        mCoveredTiles.push_back(tile);
        tile->setCoveringBuilding(this);
        return;
    }

    // We read saved state
    OD_ASSERT_TRUE(is >> tileData->mHP);

    if(tileData->mHP > 0.0)
    {
        mCoveredTiles.push_back(tile);
        tile->setCoveringBuilding(this);
    }
    else
    {
        mCoveredTilesDestroyed.push_back(tile);
    }

    GameMap* gameMap = getGameMap();
    uint32_t nbSeatsVision;
    OD_ASSERT_TRUE(is >> nbSeatsVision);
    while(nbSeatsVision > 0)
    {
        --nbSeatsVision;
        int seatId;
        OD_ASSERT_TRUE(is >> seatId);
        Seat* seat = gameMap->getSeatById(seatId);
        if(seat == nullptr)
        {
            OD_ASSERT_TRUE_MSG(false, "room=" + getName() + ", seatId=" + Helper::toString(seatId));
            continue;
        }
        tileData->mSeatsVision.push_back(seat);
    }
}

void Room::restoreInitialEntityState()
{
    // We restore the vision if we need to
    std::map<Seat*, std::vector<Tile*>> tiles;
    for(std::pair<Tile* const, TileData*>& p : mTileData)
    {
        if(p.second->mSeatsVision.empty())
            continue;
        for(Seat* seat : p.second->mSeatsVision)
        {
            seat->setVisibleBuildingOnTile(this, p.first);
            tiles[seat].push_back(p.first);
        }
    }

    // We notify the clients
    for(std::pair<Seat* const, std::vector<Tile*>>& p : tiles)
    {
        if(p.first->getPlayer() == nullptr)
            continue;
        if(!p.first->getPlayer()->getIsHuman())
            continue;

        ServerNotification *serverNotification = new ServerNotification(
            ServerNotificationType::refreshTiles, p.first->getPlayer());
        std::vector<Tile*>& tilesRefresh = p.second;
        uint32_t nbTiles = tilesRefresh.size();
        serverNotification->mPacket << nbTiles;
        for(Tile* tile : tilesRefresh)
        {
            getGameMap()->tileToPacket(serverNotification->mPacket, tile);
            p.first->exportTileToPacket(serverNotification->mPacket, tile);
        }
        ODServer::getSingleton().queueServerNotification(serverNotification);
    }
}

int Room::getCostRepair(std::vector<Tile*>& tiles)
{
    if(getCoveredTilesDestroyed().empty())
        return 0;

    if(!canBeRepaired())
        return 0;

    tiles = getCoveredTilesDestroyed();
    int nbTiles = static_cast<int>(tiles.size());
    return nbTiles * RoomManager::costPerTile(getType());
}

bool Room::sortForMapSave(Room* r1, Room* r2)
{
    // We sort room by seat id then meshName
    int seatId1 = r1->getSeat()->getId();
    int seatId2 = r2->getSeat()->getId();
    if(seatId1 == seatId2)
        return r1->getMeshName().compare(r2->getMeshName()) < 0;

    return seatId1 < seatId2;
}

void Room::buildRoomDefault(GameMap* gameMap, Room* room, const std::vector<Tile*>& tiles, Seat* seat)
{
    room->setupRoom(gameMap->nextUniqueNameRoom(room->getMeshName()), seat, tiles);
    room->addToGameMap();
    room->createMesh();

    if((seat->getPlayer() != nullptr) &&
       (seat->getPlayer()->getIsHuman()))
    {
        // We notify the clients with vision of the changed tiles. Note that we need
        // to calculate per seat since they could have vision on different parts of the building
        std::map<Seat*,std::vector<Tile*>> tilesPerSeat;
        const std::vector<Seat*>& seats = gameMap->getSeats();
        for(Seat* tmpSeat : seats)
        {
            if(tmpSeat->getPlayer() == nullptr)
                continue;
            if(!tmpSeat->getPlayer()->getIsHuman())
                continue;

            for(Tile* tile : tiles)
            {
                if(!tmpSeat->hasVisionOnTile(tile))
                    continue;

                tile->changeNotifiedForSeat(tmpSeat);
                tilesPerSeat[tmpSeat].push_back(tile);
            }
        }

        for(const std::pair<Seat* const,std::vector<Tile*>>& p : tilesPerSeat)
        {
            uint32_t nbTiles = p.second.size();
            ServerNotification serverNotification(
                ServerNotificationType::refreshTiles, p.first->getPlayer());
            serverNotification.mPacket << nbTiles;
            for(Tile* tile : p.second)
            {
                gameMap->tileToPacket(serverNotification.mPacket, tile);
                p.first->updateTileStateForSeat(tile);
                p.first->exportTileToPacket(serverNotification.mPacket, tile);
            }
            ODServer::getSingleton().sendAsyncMsg(serverNotification);
        }
    }

    room->checkForRoomAbsorbtion();
    room->updateActiveSpots();
}

int Room::getRoomCostDefault(std::vector<Tile*>& tiles, GameMap* gameMap, RoomType type,
    int tileX1, int tileY1, int tileX2, int tileY2, Player* player)
{
    std::vector<Tile*> buildableTiles = gameMap->getBuildableTilesForPlayerInArea(tileX1,
        tileY1, tileX2, tileY2, player);

    if(buildableTiles.empty())
        return RoomManager::costPerTile(type);

    int nbTiles = 0;

    for(Tile* tile : buildableTiles)
    {
        tiles.push_back(tile);
        ++nbTiles;
    }

    return nbTiles * RoomManager::costPerTile(type);
}
