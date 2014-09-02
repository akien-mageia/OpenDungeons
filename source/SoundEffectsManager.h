/*
 *  Copyright (C) 2011-2014  OpenDungeons Team
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

#ifndef SOUNDEFFECTSMANAGER_H_
#define SOUNDEFFECTSMANAGER_H_

#include <OgreSingleton.h>
#include <OgreQuaternion.h>
#include <OgreVector3.h>
#include <SFML/Audio.hpp>
#include <vector>

// Forward declarations
class CreatureDefinition;
class CreatureSound;

//! \brief A small object used to contain both the sound and its buffer,
//! as both  must have the same life-cycle.
class GameSound
{
public:
    //! \brief Game sound constructor
    //! \param filename The sound filename used to load the sound.
    //! \param spatialSound tells whether the sound should be prepared to be a spatial sound.
    //! with a position, strength and attenuation relative to the camera position.
    GameSound(const std::string& filename, bool spatialSound = true);

    ~GameSound();

    bool isInitialized() const
    { return !(mSoundBuffer == NULL); }

    //! \brief Set the sound spatial position
    void setPosition(float x, float y, float z);

    void play()
    {
        mSound.stop();
        mSound.play();
    }

    void stop()
    { mSound.stop(); }

    const std::string& getFilename() const
    { return mFilename; }

private:
    //! \brief The Main sound object
    sf::Sound mSound;

    //! \brief The corresponding sound buffer, must not be destroyed
    //! before the sound object itself is deleted.
    sf::SoundBuffer* mSoundBuffer;

    //! \brief The sound filename
    std::string mFilename;
};

//! \brief Helper class to manage sound effects.
class SoundEffectsManager: public Ogre::Singleton<SoundEffectsManager>
{
public:
    //! \brief Loads every available interface sounds
    SoundEffectsManager();

    //! \brief Deletes both sound caches.
    virtual ~SoundEffectsManager();

    //! \brief The different interface sound types.
    enum InterfaceSound
    {
        BUTTONCLICK = 0,
        DIGSELECT,
        BUILDROOM,
        BUILDTRAP,
        NUM_INTERFACE_SOUNDS
    };

    //! \brief Init the interface sounds.
    void initializeInterfaceSounds();

    //! \brief Init the default creature sounds.
    void initializeDefaultCreatureSounds();

    void setListenerPosition(const Ogre::Vector3& position, const Ogre::Quaternion& orientation);

    void playInterfaceSound(InterfaceSound soundType);
    void playInterfaceSound(InterfaceSound soundType, const Ogre::Vector3& position);
    void playInterfaceSound(InterfaceSound soundType, int tileX, int tileY);

    //! \brief Play a random rock falling sound at the given position.
    void playRockFallingSound(int tileX, int tileY);

    //! \brief Play a random claimed sound at the given position.
    void playClaimedSound(int tileX, int tileY);

    //! \brief Gives the creature sounds list relative to the creature class.
    //! \warning The CreatureSound* object is to be deleted only by the sound manager.
    CreatureSound* getCreatureClassSounds(const std::string& className);

private:
    //! \brief Independent interface sounds, such as clicks.
    std::vector<GameSound*> mInterfaceSounds;

    //! \brief Independant rocks fall sounds
    std::vector<GameSound*> mRocksFallSounds;

    //! \brief Independant claimed tile sounds
    std::vector<GameSound*> mClaimedSounds;

    //! \brief The sound cache, containing the spatial sound references, used by game entities.
    //! \brief The GameSounds here must be deleted at destruction.
    std::map<std::string, GameSound*> mGameSoundCache;

    //! \brief The list of available sound effects per creature class.
    //! \brief The CreatureSounds here must be deleted at destruction.
    std::map<std::string, CreatureSound*> mCreatureSoundCache;

    //! \brief Create a new creature sound list for the given class and register it to the cache.
    void createCreatureClassSounds(const std::string& className);
};

#endif // SOUNDEFFECTSMANAGER_H_