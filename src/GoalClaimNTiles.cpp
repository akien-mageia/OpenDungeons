#include <sstream>
#include <iostream>

#include "Globals.h"
#include "GoalClaimNTiles.h"
#include "GameMap.h"
#include "Player.h"

GoalClaimNTiles::GoalClaimNTiles(const std::string& nName,
        const std::string& nArguments) :
        Goal(nName, nArguments),
        numberOfTiles(atoi(nArguments.c_str()))
{
    std::cout << "\nAdding goal " << getName();
}

bool GoalClaimNTiles::isMet(Seat *s)
{
    return (s->getNumClaimedTiles() >= numberOfTiles);
}

std::string GoalClaimNTiles::getSuccessMessage()
{
    std::stringstream tempSS;
    tempSS << "You have claimed more than " << numberOfTiles << " tiles.";
    return tempSS.str();
}

std::string GoalClaimNTiles::getFailedMessage()
{
    std::stringstream tempSS;
    tempSS << "You have failed to claim more than " << numberOfTiles
            << " tiles.";
    return tempSS.str();
}

std::string GoalClaimNTiles::getDescription()
{
    std::stringstream tempSS;
    tempSS << "Claimed " << gameMap.me->seat->getNumClaimedTiles() << " of "
            << numberOfTiles << " tiles.";
    return tempSS.str();
}

