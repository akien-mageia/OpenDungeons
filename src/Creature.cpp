#include <math.h>
#include <algorithm>
using namespace std;

#include "Creature.h"
#include "Defines.h"
#include "Globals.h"
#include "Functions.h"
#include "CreatureAction.h"
#include "Network.h"
#include "Field.h"
#include "Weapon.h"

Creature::Creature()
{
	sem_init(&meshCreationFinishedSemaphore, 0, 0);
	sem_init(&meshDestructionFinishedSemaphore, 0, 0);
	hasVisualDebuggingEntities = false;
	position = Ogre::Vector3(0,0,0);
	scale = Ogre::Vector3(1,1,1);
	sightRadius = 10.0;
	digRate = 10.0;
	destinationX = 0;
	destinationY = 0;

	hp = 10;
	mana = 10;
	sightRadius = 10;
	digRate = 10;
	moveSpeed = 1.0;
	tilePassability = Tile::walkableTile;

	weaponL = NULL;
	weaponR = NULL;

	animationState = NULL;

	actionQueue.push_back(CreatureAction(CreatureAction::idle));
	battleField = new Field("autoname");
}

Creature::Creature(string nClassName, string nMeshName, Ogre::Vector3 nScale, int nHP, int nMana, double nSightRadius, double nDigRate, double nMoveSpeed)
{
	// This constructor is meant to be used to initialize a creature class so no creature specific stuff should be set
	className = nClassName;
	meshName = nMeshName;
	scale = nScale;

	hp = nHP;
	mana = nMana;
	sightRadius = nSightRadius;
	digRate = nDigRate;
	moveSpeed = nMoveSpeed;
	tilePassability = Tile::walkableTile;
}

/*! \brief A matched function to transport creatures between files and over the network.
 *
 */
ostream& operator<<(ostream& os, Creature *c)
{
	os << c->className << "\t" << c->name << "\t";
	os << c->position.x << "\t" << c->position.y << "\t" << c->position.z << "\t";
	os << c->color << "\t";
	os << c->weaponR << "\t" << c->weaponL;

	return os;
}

/*! \brief A matched function to transport creatures between files and over the network.
 *
 */
istream& operator>>(istream& is, Creature *c)
{
	static int uniqueNumber = 1;
	double xLocation = 0.0, yLocation = 0.0, zLocation = 0.0;
	string tempString;
	is >> c->className;

	is >> tempString;

	if(tempString.compare("autoname") == 0)
	{
		char tempArray[255];
		snprintf(tempArray, sizeof(tempArray), "%s_%04i", c->className.c_str(), uniqueNumber);
		tempString = string(tempArray);
		uniqueNumber++;
	}

	c->name = tempString;

	is >> xLocation >> yLocation >> zLocation;
	c->position = Ogre::Vector3(xLocation, yLocation, zLocation);
	is >> c->color;

	c->weaponL = new Weapon;
	is >> c->weaponL;
	c->weaponL->parentCreature = c;
	c->weaponL->handString = "R";

	c->weaponR = new Weapon;
	is >> c->weaponR;
	c->weaponR->parentCreature = c;
	c->weaponR->handString = "L";

	// Copy the class based items
	Creature *creatureClass = gameMap.getClassDescription(c->className);
	if(creatureClass != NULL)
	{
		c->meshName = creatureClass->meshName;
		c->scale = creatureClass->scale;
		c->sightRadius = creatureClass->sightRadius;
		c->digRate = creatureClass->digRate;
		c->hp = creatureClass->hp;
		c->mana = creatureClass->mana;
		c->moveSpeed = creatureClass->moveSpeed;
	}

	return is;
}

/*! \brief Allocate storage for, load, and inform OGRE about a mesh for this creature.
 *
 *  This function is called after a creature has been loaded from hard disk,
 *  received from a network connection, or created during the game play by the
 *  game engine itself.
 */
void Creature::createMesh()
{
	//NOTE: I think this line is redundant since the a sem_wait on any previous destruction should return the sem to 0 anyway but this takes care of it in case it is forgotten somehow
	sem_init(&meshCreationFinishedSemaphore, 0, 0);
	sem_init(&meshDestructionFinishedSemaphore, 0, 0);

	RenderRequest *request = new RenderRequest;
	request->type = RenderRequest::createCreature;
	request->p = this;

	sem_wait(&renderQueueSemaphore);
	renderQueue.push_back(request);
	sem_post(&renderQueueSemaphore);

	//FIXME:  This function needs to wait until the render queue has processed the request before returning.  This should fix the bug where the client crashes loading levels with lots of creatures.  Other create mesh routines should have a similar wait statement.  It currently breaks the program since this function gets called from the rendering thread causing the thread to wait for itself to do something.
	//sem_wait(&meshCreationFinishedSemaphore);

}


/*! \brief Free the mesh and inform the OGRE system that the mesh has been destroyed.
 *
 *  This function is primarily a helper function for other methods.
 */
void Creature::destroyMesh()
{
	weaponL->destroyMesh();
	weaponR->destroyMesh();

	//NOTE: I think this line is redundant since the a sem_wait on any previous creation should return the sem to 0 anyway but this takes care of it in case it is forgotten somehow
	//sem_init(&meshCreationFinishedSemaphore, 0, 0);

	RenderRequest *request = new RenderRequest;
	request->type = RenderRequest::destroyCreature;
	request->p = this;

	sem_wait(&renderQueueSemaphore);
	renderQueue.push_back(request);
	sem_post(&renderQueueSemaphore);
	sem_wait(&meshDestructionFinishedSemaphore);
}

/*! \brief Changes the creatures position to a new position.
 *
 *  This is an overloaded function which just calls Creature::setPosition(double x, double y, double z).
 */
void Creature::setPosition(Ogre::Vector3 v)
{
	setPosition(v.x, v.y, v.z);
}

/*! \brief Changes the creatures position to a new position.
 *
 *  Moves the creature to a new location in 3d space.  This function is
 *  responsible for informing OGRE anything it needs to know, as well as
 *  maintaining the list of creatures in the individual tiles.
 */
void Creature::setPosition(double x, double y, double z)
{
	SceneNode *creatureSceneNode = mSceneMgr->getSceneNode(name + "_node");

	//creatureSceneNode->setPosition(x/BLENDER_UNITS_PER_OGRE_UNIT, y/BLENDER_UNITS_PER_OGRE_UNIT, z/BLENDER_UNITS_PER_OGRE_UNIT);
	creatureSceneNode->setPosition(x, y, z);

	// If we are on the gameMap we may need to update the tile we are in
	if(gameMap.getCreature(name) != NULL)
	{
		// We are on the map
		// Move the creature relative to its parent scene node.  We record the
		// tile the creature is in before and after the move to properly
		// maintain the results returned by the positionTile() function.
		Tile *oldPositionTile = positionTile();
		position = Ogre::Vector3(x, y, z);
		Tile *newPositionTile = positionTile();

		if(oldPositionTile != newPositionTile)
		{
			if(oldPositionTile != NULL)
				oldPositionTile->removeCreature(this);

			if(positionTile() != NULL)
				positionTile()->addCreature(this);
		}
	}
	else
	{
		// We are not on the map
		position = Ogre::Vector3(x, y, z);
	}
}

/*! \brief A simple accessor function to get the creature's current position in 3d space.
 *
 */
Ogre::Vector3 Creature::getPosition()
{
	return position;
}

/*! \brief The main AI routine which decides what the creature will do and carries out that action.
 *
 * The doTurn routine is the heart of the Creature AI subsystem.  The other,
 * higher level, functions such as GameMap::doTurn() ultimately just call this
 * function to make the creatures act.
 *
 * The function begins in a pre-cognition phase which prepares the creature's
 * brain state for decision making.  This involves generating lists of known
 * about creatures, either through sight, hearing, keeper knowledge, etc, as
 * well as some other bookkeeping stuff.
 *
 * Next the function enters the cognition phase where the creature's current
 * state is examined and a decision is made about what to do.  The state of the
 * creature is in the form of a queue, which is really used more like a stack.
 * At the beginning of the game the 'idle' action is pushed onto each
 * creature's actionQueue, this action is never removed from the tail end of
 * the queue and acts as a "last resort" for when the creature completely runs
 * out of things to do.  Other actions such as 'walkToTile' or 'attackCreature'
 * are then pushed onto the front of the queue and will determine the
 * creature's future behavior.  When actions are complete they are popped off
 * the front of the action queue, causing the creature to revert back into the
 * state it was in when the actions was placed onto the queue.  This allows
 * actions to be carried out recursively, i.e. if a creature is trying to dig a
 * tile and it is not nearby it can begin walking toward the tile as a new
 * action, and when it arrives at the tile it will revert to the 'digTile'
 * action.
 *
 * In the future there should also be a post-cognition phase to do any
 * additional checks after it tries to move, etc.
 */
void Creature::doTurn()
{
	vector<Tile*> markedTiles;
	list<Tile*>walkPath;
	list<Tile*>basePath;
	list<Tile*>::iterator tileListItr;
	vector< list<Tile*> > possiblePaths;
	vector< list<Tile*> > shortPaths;
	bool loopBack;
	int tempInt;

	// If we are not standing somewhere on the map, do nothing.
	if(positionTile() == NULL)
		return;

	// Look at the surrounding area
	updateVisibleTiles();
	vector<Creature*> visibleEnemies = getVisibleEnemies();
	vector<Creature*> visibleAllies = getVisibleAllies();

	// If the creature can see enemies
	if(visibleEnemies.size() > 0)
	{
		cout << "\nCreature sees enemies:  " << visibleEnemies.size() << "   " << name;
		cout << "\nvisibleEnemies:\n";

		for(unsigned int i = 0; i < visibleEnemies.size(); i++)
		{
			cout << visibleEnemies[i] << endl;
		}

		// if we are not already fighting with a creature
		if(actionQueue.size() > 0 && actionQueue.front().type != CreatureAction::attackCreature && randomDouble(0.0, 1.0) > 0.3)
		{
			CreatureAction tempAction;
			tempAction.creature = visibleEnemies[0];
			tempAction.type = CreatureAction::attackCreature;
			actionQueue.push_front(tempAction);
		}
	}

	// The loopback variable allows creatures to begin processing a new
	// action immediately after some other action happens.
	do
	{
		loopBack = false;

		// Carry out the current task
		double diceRoll;
		double tempDouble;
		Tile *neighborTile;
		vector<Tile*>neighbors, creatureNeighbors;
		bool wasANeighbor = false;
		Player *tempPlayer;
		Tile *tempTile, *myTile;
		list<Tile*> tempPath;
		pair<LocationType, double> min;

		diceRoll = randomDouble(0.0, 1.0);
		if(actionQueue.size() > 0)
		{
			switch(actionQueue.front().type)
			{
				case CreatureAction::idle:
					//cout << "idle ";
					setAnimationState("Idle");
					//FIXME: make this into a while loop over a vector of <action, probability> pairs

					// Decide to check for diggable tiles with some probability
					if(diceRoll < 0.4 && digRate > 0.1)
					{
						//loopBack = true;
						actionQueue.push_front(CreatureAction(CreatureAction::digTile));
						loopBack = true;
					}

					// Decide to "wander" a short distance
					else if(diceRoll < 0.6)
					{
						//loopBack = true;
						actionQueue.push_front(CreatureAction(CreatureAction::walkToTile));
						int tempX = position.x + 2.0*gaussianRandomDouble();
						int tempY = position.y + 2.0*gaussianRandomDouble();

						Tile *tempPositionTile = positionTile();
						list<Tile*> result;
						if(tempPositionTile != NULL)
						{
							result = gameMap.path(tempPositionTile->x, tempPositionTile->y, tempX, tempY, tilePassability);
						}

						if(result.size() >= 2)
						{
							setAnimationState("Walk");
							gameMap.cutCorners(result, tilePassability);
							list<Tile*>::iterator itr = result.begin();
							itr++;
							while(itr != result.end())
							{
								addDestination((*itr)->x, (*itr)->y);
								itr++;
							}
						}
					}
					else
					{
						// Remain idle
						//setAnimationState("Idle");
					}

					break;

				case CreatureAction::walkToTile:
					//TODO: Peek at the item that caused us to walk
					if(actionQueue[1].type == CreatureAction::digTile)
					{
						tempPlayer = getControllingPlayer();
						// Check to see if the tile is still marked for digging
						int index = walkQueue.size();
						Tile *currentTile = gameMap.getTile((int)walkQueue[index].x, (int)walkQueue[index].y);
						if(currentTile != NULL)
						{
							// If it is not marked
							if(tempPlayer != NULL && !currentTile->getMarkedForDigging(tempPlayer))
							{
								// Clear the walk queue
								clearDestinations();
							}
						}
					}

					//cout << "walkToTile ";
					if(walkQueue.size() == 0)
					{
						actionQueue.pop_front();
						loopBack = true;
					}
					break;

				case CreatureAction::digTile:
					tempPlayer = getControllingPlayer();
					//cout << "dig ";

					// Find visible tiles, marked for digging
					for(unsigned int i = 0; i < visibleTiles.size(); i++)
					{
						// Check to see if the tile is marked for digging
						if(tempPlayer != NULL && visibleTiles[i]->getMarkedForDigging(tempPlayer))
						{
							markedTiles.push_back(visibleTiles[i]);
						}
					}

					// See if any of the tiles is one of our neighbors
					wasANeighbor = false;
					creatureNeighbors = gameMap.neighborTiles(position.x, position.y);
					for(unsigned int i = 0; i < creatureNeighbors.size() && !wasANeighbor; i++)
					{
						if(tempPlayer != NULL && creatureNeighbors[i]->getMarkedForDigging(tempPlayer))
						{
							setAnimationState("Dig");
							creatureNeighbors[i]->setFullness(creatureNeighbors[i]->getFullness() - digRate);

							// Force all the neighbors to recheck their meshes as we may have exposed
							// a new side that was not visible before.
							neighbors = gameMap.neighborTiles(creatureNeighbors[i]->x, creatureNeighbors[i]->y);
							for(unsigned int j = 0; j < neighbors.size(); j++)
							{
								neighbors[j]->setFullness(neighbors[j]->getFullness());
							}

							if(creatureNeighbors[i]->getFullness() < 0)
							{
								creatureNeighbors[i]->setFullness(0);
							}

							// If the tile has been dug out, move into that tile and idle
							if(creatureNeighbors[i]->getFullness() == 0)
							{
								addDestination(creatureNeighbors[i]->x, creatureNeighbors[i]->y);
								setAnimationState("Walk");

								// Remove the dig action and replace it with
								// walking to the newly dug out tile.
								actionQueue.pop_front();
								actionQueue.push_front(CreatureAction(CreatureAction::walkToTile));
							}

							wasANeighbor = true;
							break;
						}
					}

					if(wasANeighbor)
						break;

					// Find paths to all of the neighbor tiles for all of the marked visible tiles.
					possiblePaths.clear();
					for(unsigned int i = 0; i < markedTiles.size(); i++)
					{
						neighbors = gameMap.neighborTiles(markedTiles[i]->x, markedTiles[i]->y);
						for(unsigned int j = 0; j < neighbors.size(); j++)
						{
							neighborTile = neighbors[j];
							if(neighborTile != NULL && neighborTile->getFullness() == 0)
								possiblePaths.push_back(gameMap.path(positionTile()->x, positionTile()->y, neighborTile->x, neighborTile->y, tilePassability));

						}
					}

					// Find the shortest path and start walking toward the tile to be dug out
					if(possiblePaths.size() > 0)
					{
						// Find the N shortest valid paths, see if there are any valid paths shorter than this first guess
						shortPaths.clear();
						for(unsigned int i = 0; i < possiblePaths.size(); i++)
						{
							// If the current path is long enough to be valid
							unsigned int currentLength = possiblePaths[i].size();
							if(currentLength >= 2)
							{
								shortPaths.push_back(possiblePaths[i]);

								// If we already have enough short paths
								if(shortPaths.size() > 5)
								{
									unsigned int longestLength, longestIndex;

									// Kick out the longest
									longestLength = shortPaths[0].size();
									longestIndex = 0;
									for(unsigned int j = 1; j < shortPaths.size(); j++)
									{
										if(shortPaths[j].size() > longestLength)
										{
											longestLength = shortPaths.size();
											longestIndex = j;
										}
									}

									shortPaths.erase(shortPaths.begin() + longestIndex);
								}
							}
						}

						// Randomly pick a short path to take
						unsigned int numShortPaths = shortPaths.size();
						if(numShortPaths > 0)
						{
							unsigned int shortestIndex;
							shortestIndex = (int)randomDouble(0, (double)numShortPaths-0.001);
							walkPath = shortPaths[shortestIndex];

							// If the path is a legitimate path, walk down it to the tile to be dug out
							if(walkPath.size() >= 2)
							{
								setAnimationState("Walk");
								gameMap.cutCorners(walkPath, tilePassability);
								list<Tile*>::iterator itr = walkPath.begin();
								itr++;
								while(itr != walkPath.end())
								{
									addDestination((*itr)->x, (*itr)->y);
									itr++;
								}

								actionQueue.push_front(CreatureAction(CreatureAction::walkToTile));
								break;
							}
						}
					}

					// If none of our neighbors are marked for digging we got here too late.
					// Finish digging
					if(actionQueue.front().type == CreatureAction::digTile)
					{
						actionQueue.pop_front();
						loopBack = true;
					}
					break;

				case CreatureAction::attackCreature:
					// If there are no more enemies visible, stop attacking
					if(visibleEnemies.size() == 0)
					{
						actionQueue.pop_front();
						loopBack = true;
						break;
					}

					// Find the first enemy close enough to hit and attack it
					for(unsigned int i = 0; i < visibleEnemies.size(); i++)
					{
						Tile *tempTile = visibleEnemies[i]->positionTile();
						double rSquared = powl(myTile->x - tempTile->x, 2.0) + powl(myTile->y - tempTile->y, 2.0);
						if(sqrt(rSquared) < max(weaponL->range, weaponR->range))
						{
							double damageDone = weaponL->damage + weaponR->damage;
							visibleEnemies[i]->hp -= damageDone;
							break;
						}
					}

					// If we exited the above loop early it means we attacked
					// a creature and are done with this turn
					/*
					if(i < visibleEnemies.size())
					{
						break;
					}
					*/

					// Loop over the tiles in this creatures battleField and compute their value.
					// The creature will then walk towards the tile with the minimum value.
					myTile = positionTile();
					battleField->clear();
					for(unsigned int i = 0; i < visibleTiles.size(); i++)
					{
						tempTile = visibleTiles[i];
						double rSquared = tempTile->x*tempTile->x + tempTile->y*tempTile->y;
						double tileValue = 0.0;// - sqrt(rSquared)/sightRadius;

						// Enemies
						for(unsigned int j = 0; j < visibleEnemies.size(); j++)
						{
							Tile *tempTile2 = visibleEnemies[j]->positionTile();

							// Compensate for how close the creature is to me
							rSquared = powl(myTile->x - tempTile2->x, 2.0) + powl(myTile->y - tempTile2->y, 2.0);
							double factor = 1.0 / (sqrt(rSquared) + 1.0);

							// Subtract for the distance from the enemy creature to r
							rSquared = powl(tempTile->x - tempTile2->x, 2.0) + powl(tempTile->y - tempTile2->y, 2.0);
							tileValue += factor*sqrt(rSquared);
						}

						// Allies
						for(unsigned int j = 0; j < visibleAllies.size(); j++)
						{
							Tile *tempTile2 = visibleAllies[j]->positionTile();

							// Compensate for how close the creature is to me
							rSquared = powl(myTile->x - tempTile2->x, 2.0) + powl(myTile->y - tempTile2->y, 2.0);
							double factor = 1.0 / (sqrt(rSquared) + 1.0);

							rSquared = powl(tempTile->x - tempTile2->x, 2.0) + powl(tempTile->y - tempTile2->y, 2.0);
							tileValue += 15.0 / (sqrt(rSquared+1.0));
						}

						double jitter = 0.05;
						double tileScaleFactor = 0.05;
						battleField->set(tempTile->x, tempTile->y, (tileValue + randomDouble(-1.0*jitter, jitter))*tileScaleFactor);
					}

					clearDestinations();
				       	min = battleField->min();
					tempDouble = 4;
					tempPath = gameMap.path(positionTile()->x, positionTile()->y, min.first.first + randomDouble(-1.0*tempDouble,tempDouble), min.first.second + randomDouble(-1.0*tempDouble, tempDouble), tilePassability);
					tempInt = 3;
					if(tempPath.size() > tempInt+2)
					{
						list<Tile*>::iterator itr = tempPath.begin();
						int count = 0;
						while(itr != tempPath.end() && count < tempInt)
						{
							itr++;
							count++;
						}
						addDestination((*itr)->x, (*itr)->y);
					}

					if(battleField->name.compare("field_1") == 0)
					{
						battleField->refreshMeshes(0.0);
					}
					break;

				default:
					cerr << "\n\nERROR:  Unhandled action type in Creature::doTurn().\n\n";
					exit(1);
					break;
			}
		}
		else
		{
			cerr << "\n\nERROR:  Creature has empty action queue in doTurn(), this should not happen.\n\n";
			exit(1);
		}

	} while(loopBack);

	// Update the visual debugging entities
	if(hasVisualDebuggingEntities)
	{
		// if we are standing in a different tile than we were last turn
		Tile *currentPositionTile = positionTile();
		if(currentPositionTile != previousPositionTile)
		{
			//TODO:  This destroy and re-create is kind of a hack as its likely only a few tiles will actually change.
			destroyVisualDebugEntities();
			createVisualDebugEntities();
		}
	}
}

/*! \brief Creates a list of Tile pointers in visibleTiles
 *
 * The tiles are currently determined to be visible or not, according only to
 * the distance they are away from the creature.  Because of this they can
 * currently see through walls, etc.
*/
void Creature::updateVisibleTiles()
{
	//int xMin, yMin, xMax, yMax;
	const double sightRadiusSquared = sightRadius * sightRadius;
	Tile *tempPositionTile = positionTile();
	Tile *currentTile;
	int xBase = tempPositionTile->x;
	int yBase = tempPositionTile->y;
	int xLoc, yLoc;

	visibleTiles.clear();

	// Add the tile the creature is standing in
	if(tempPositionTile != NULL)
	{
		visibleTiles.push_back(tempPositionTile);
	}

	// Add the 4 principle axes rays
	for(int i = 1; i < sightRadius; i++)
	{
		for(int j = 0; j < 4; j++)
		{
			switch(j)
			{
				case 0:  xLoc = xBase+i;   yLoc = yBase;  break;
				case 1:  xLoc = xBase-i;   yLoc = yBase;  break;
				case 2:  xLoc = xBase;   yLoc = yBase+i;  break;
				case 3:  xLoc = xBase;   yLoc = yBase-i;  break;
			}

			currentTile = gameMap.getTile(xLoc, yLoc);

			if(currentTile != NULL)
			{
				// Check if we can actually see the tile in question
				// or if it is blocked by terrain
				if(tempPositionTile != NULL && gameMap.pathIsClear(gameMap.lineOfSight(tempPositionTile->x, tempPositionTile->y, xLoc, yLoc), Tile::flyableTile))
				{
					visibleTiles.push_back(currentTile);
				}
			}
		}
	}

	// Fill in the 4 pie slice shaped sectors
	for(int i = 1; i < sightRadius; i++)
	{
		for(int j = 1; j < sightRadius; j++)
		{
			// Check to see if the current tile is actually close enough to be visible
			int distSQ = i*i + j*j;
			if(distSQ < sightRadiusSquared)
			{
				for(int k = 0; k < 4; k++)
				{
					switch(k)
					{
						case 0:  xLoc = xBase+i;   yLoc = yBase+j;  break;
						case 1:  xLoc = xBase+i;   yLoc = yBase-j;  break;
						case 2:  xLoc = xBase-i;   yLoc = yBase+j;  break;
						case 3:  xLoc = xBase-i;   yLoc = yBase-j;  break;
					}

					currentTile = gameMap.getTile(xLoc, yLoc);
					
					if(currentTile != NULL)
					{
						// Check if we can actually see the tile in question
						// or if it is blocked by terrain
						if(tempPositionTile != NULL && gameMap.pathIsClear(gameMap.lineOfSight(tempPositionTile->x, tempPositionTile->y, xLoc, yLoc), Tile::flyableTile))
						{
							visibleTiles.push_back(currentTile);
						}
					}
				}
			}
			else
			{
				// If this tile is too far away then any tile with a j value greater than this
				// will also be too far away.  Setting j=sightRadius will break out of the inner loop
				j = sightRadius;
			}
		}
	}

	//TODO:  Add the sector shaped region of the visible region
}

/*! \brief Loops over the visibleTiles and adds all enemy creatures in each tile to a list which it returns.
 *
*/
vector<Creature*> Creature::getVisibleEnemies()
{
	return getVisibleForce(color, true);
}

vector<Creature*> Creature::getVisibleAllies()
{
	return getVisibleForce(color, false);
}

vector<Creature*> Creature::getVisibleForce(int color, bool invert)
{
	vector<Creature*> returnList;

	// Loop over the visible tiles
	vector<Tile*>::iterator itr;
	for(itr = visibleTiles.begin(); itr != visibleTiles.end(); itr++)
	{
		// Loop over the creatures in the given tile
		for(int i = 0; i < (*itr)->numCreaturesInCell(); i++)
		{
			Creature *tempCreature = (*itr)->getCreature(i);
			// If it is an enemy
			if(tempCreature != NULL)
			{
				if(invert)
				{
					if(tempCreature->color != color)
					{
						// Add the current creature
						returnList.push_back(tempCreature);
					}
				}
				else
				{
					if(tempCreature->color == color)
					{
						// Add the current creature
						returnList.push_back(tempCreature);
					}
				}
			}
		}
	}

	return returnList;
}

/*! \brief Displays a mesh on all of the tiles visible to the creature.
 *
*/
void Creature::createVisualDebugEntities()
{
	hasVisualDebuggingEntities = true;
	visualDebugEntityTiles.clear();

	Tile *currentTile = NULL;
	updateVisibleTiles();
	for(unsigned int i = 0; i < visibleTiles.size(); i++)
	{
		currentTile = visibleTiles[i];

		if(currentTile != NULL)
		{
			// Create a mesh for the current visible tile
			RenderRequest *request = new RenderRequest;
			request->type = RenderRequest::createCreatureVisualDebug;
			request->p = currentTile;
			request->p2 = this;

			visualDebugEntityTiles.push_back(currentTile);

			sem_wait(&renderQueueSemaphore);
			renderQueue.push_back(request);
			sem_post(&renderQueueSemaphore);
		}
	}
}

/*! \brief Destroy the meshes created by createVisualDebuggingEntities().
 *
*/
void Creature::destroyVisualDebugEntities()
{
	hasVisualDebuggingEntities = false;

	Tile *currentTile = NULL;
	updateVisibleTiles();
	list<Tile*>::iterator itr;
	for(itr = visualDebugEntityTiles.begin(); itr != visualDebugEntityTiles.end(); itr++)
	{
		currentTile = *itr;

		if(currentTile != NULL)
		{
			// Destroy the mesh for the current visible tile
			RenderRequest *request = new RenderRequest;
			request->type = RenderRequest::destroyCreatureVisualDebug;
			request->p = currentTile;
			request->p2 = this;

			sem_wait(&renderQueueSemaphore);
			renderQueue.push_back(request);
			sem_post(&renderQueueSemaphore);
		}
	}

}

/*! \brief Returns a pointer to the tile the creature is currently standing in.
 *
 * 
*/
Tile* Creature::positionTile()
{
	return gameMap.getTile((int)(position.x + 0.4999), (int)(position.y + 0.4999));
}

/*! \brief Completely destroy this creature, including its OGRE entities, scene nodes, etc.
 *
 * 
*/
void Creature::deleteYourself()
{
	weaponL->destroyMesh();
	weaponR->destroyMesh();

	if(positionTile() != NULL)
		positionTile()->removeCreature(this);

	RenderRequest *request = new RenderRequest;
	request->type = RenderRequest::destroyCreature;
	request->p = this;

	RenderRequest *request2 = new RenderRequest;
	request2->type = RenderRequest::deleteCreature;
	request2->p = this;

	sem_wait(&renderQueueSemaphore);
	renderQueue.push_back(request);
	renderQueue.push_back(request2);
	sem_post(&renderQueueSemaphore);
}

/*! \brief Sets a new animation state from the creature's library of animations.
 *
 * 
*/
void Creature::setAnimationState(string s)
{
	string tempString;
	stringstream tempSS;
	RenderRequest *request = new RenderRequest;
	request->type = RenderRequest::setCreatureAnimationState;
	request->p = this;
	request->str = s;

	if(serverSocket != NULL)
	{
		try
		{
			// Place a message in the queue to inform the clients about the new animation state
			ServerNotification *serverNotification = new ServerNotification;
			serverNotification->type = ServerNotification::creatureSetAnimationState;
			serverNotification->str = s;
			serverNotification->cre = this;

			sem_wait(&serverNotificationQueueLockSemaphore);
			serverNotificationQueue.push_back(serverNotification);
			sem_post(&serverNotificationQueueLockSemaphore);

			sem_post(&serverNotificationQueueSemaphore);
		}
		catch(bad_alloc&)
		{
			cerr << "\n\nERROR:  bad alloc in Creature::setAnimationState\n\n";
			exit(1);
		}
	}

	sem_wait(&renderQueueSemaphore);
	renderQueue.push_back(request);
	sem_post(&renderQueueSemaphore);
}

/*! \brief Returns the creature's currently active animation state.
 *
 * 
*/
AnimationState* Creature::getAnimationState()
{
	return animationState;
}

/*! \brief Adds a position in 3d space to the creature's walk queue and, if necessary, starts it walking.
 *
 * This function also places a message in the serverNotificationQueue so that
 * relevant clients are informed about the change.
 * 
*/
void Creature::addDestination(int x, int y)
{
	//cout << "w(" << x << ", " << y << ") ";
	Ogre::Vector3 destination(x, y, 0);

	// if there are currently no destinations in the walk queue
	if(walkQueue.size() == 0)
	{
		// Add the destination and set the remaining distance counter
		walkQueue.push_back(destination);
		shortDistance = position.distance(walkQueue.front());

		// Rotate the creature to face the direction of the destination
		walkDirection = walkQueue.front() - position;
		walkDirection.normalise();

		//TODO:  this is OGRE rendering code and it should be moved to the RenderRequest system
		SceneNode *node = mSceneMgr->getSceneNode(name + "_node");
		Ogre::Vector3 src = node->getOrientation() * Ogre::Vector3::NEGATIVE_UNIT_Y;

		// Work around 180 degree quaternion rotation quirk
		if ((1.0f + src.dotProduct(walkDirection)) < 0.0001f)
		{
			node->roll(Degree(180));
		}
		else
		{
			Quaternion quat = src.getRotationTo(walkDirection);
			node->rotate(quat);
		}
	}
	else
	{
		// Add the destination
		walkQueue.push_back(destination);
	}

	if(serverSocket != NULL)
	{
		try
		{
			// Place a message in the queue to inform the clients about the new destination
			ServerNotification *serverNotification = new ServerNotification;
			serverNotification->type = ServerNotification::creatureAddDestination;
			serverNotification->str = name;
			serverNotification->vec = destination;

			sem_wait(&serverNotificationQueueLockSemaphore);
			serverNotificationQueue.push_back(serverNotification);
			sem_post(&serverNotificationQueueLockSemaphore);

			sem_post(&serverNotificationQueueSemaphore);
		}
		catch(bad_alloc&)
		{
			cerr << "\n\nERROR:  bad alloc in Creature::addDestination\n\n";
			exit(1);
		}
	}
}

/*! \brief Clears all future destinations from the walk queue, stops the creature where it is, and sets its animation state.
 *
*/
void Creature::clearDestinations()
{
	walkQueue.clear();
	stopWalking();

	if(serverSocket != NULL)
	{
		// Place a message in the queue to inform the clients about the clear
		ServerNotification *serverNotification = new ServerNotification;
		serverNotification->type = ServerNotification::creatureClearDestinations;
		serverNotification->cre = this;

		sem_wait(&serverNotificationQueueLockSemaphore);
		serverNotificationQueue.push_back(serverNotification);
		sem_post(&serverNotificationQueueLockSemaphore);

		sem_post(&serverNotificationQueueSemaphore);
	}
}

/*! \brief Stops the creature where it is, and sets its animation state.
 *
*/
void Creature::stopWalking()
{
	walkDirection = Ogre::Vector3::ZERO;
	setAnimationState("Idle");
}

/*! \brief An accessor to return whether or not the creature has OGRE entities for its visual debugging entities.
 *
*/
bool Creature::getHasVisualDebuggingEntities()
{
	return hasVisualDebuggingEntities;
}

/*! \brief Returns the first player whose color matches this creature's color.
 *
*/
Player* Creature::getControllingPlayer()
{
	Player *tempPlayer;

	if(gameMap.me->seat->color == color)
	{
		return gameMap.me;
	}

	// Try to find and return a player with color equal to this creature's
	for(unsigned int i = 0; i < gameMap.numPlayers(); i++)
	{
		tempPlayer = gameMap.getPlayer(i);
		if(tempPlayer->seat->color == color)
		{
			return tempPlayer;
		}
	}

	// No player found, return NULL
	return NULL;
}

/*! \brief Clears the action queue, except for the Idle action at the end.
 *
*/
void Creature::clearActionQueue()
{
	actionQueue.clear();
	actionQueue.push_back(CreatureAction(CreatureAction::idle));
}

