/*!
 * \file   CameraManager.h
 * \date:  02 July 2011
 * \author StefanP.MUC
 * \brief  Handles the camera movements
 */

#ifndef CAMERAMANAGER_H_
#define CAMERAMANAGER_H_

#include "GameMap.h"
#include "AbstractApplicationMode.h"
#include "HermiteCatmullSpline.h"



#include <OgrePrerequisites.h>
#include <OgreVector3.h>
#include <OgreCamera.h>
#include <OgreSingleton.h>
#include <OgreSceneNode.h>
#include <OgreRay.h>
#include <OgrePlane.h>
#include <OgreString.h>

#include <set>

class ModeManager;
class Console;


class CameraManager : public Ogre::Singleton<CameraManager>
    {
    friend class Console;


    public:
    enum Direction
	{
        moveLeft, moveRight, moveForward, moveBackward, moveUp, moveDown,
        rotateLeft, rotateRight, rotateUp, rotateDown,

        stopLeft, stopRight, stopForward, stopBackward, stopUp, stopDown,
        stopRotLeft, stopRotRight, stopRotUp, stopRotDown,
	

	randomRotateX,	zeroRandomRotateX,
	randomRotateY,	zeroRandomRotateY,
        fullStop
	};
    
    static const int HIDE =  1;
    static const int SHOW =  2;

    HermiteCatmullSpline xHCS;
    HermiteCatmullSpline yHCS; 



    struct Vector3i{
	Vector3i(const Ogre::Vector3& OV){x = (1<<10) * OV.x ; y = (1<<10) * OV.y; z = (1<<10) *OV.z ;  }


	int x ; int y ; int z;};


    CameraManager(Ogre::Camera* cam, GameMap* gm = NULL);

    inline void setCircleCenter( int XX, int YY) {centerX = XX ; centerY = YY;} ;
    inline void setCircleRadious(unsigned int rr){ radious = rr;};
    inline void setCircleMode(bool mm){circleMode = mm ; alpha = 0;};
    inline void setCatmullSplineMode(bool mm){catmullSplineMode = mm;  alpha = 0; };
    inline bool switchPM(){ switchedPM = true;  };


    void setModeManager(ModeManager* mm){modeManager = mm;};

    
    //get/set moveSpeed
    inline const Ogre::Real& getMoveSpeed() const {
        return moveSpeed;
	}
    inline void setMoveSpeed(const Ogre::Real& newMoveSpeed) {
        moveSpeed = newMoveSpeed;
	}


    //get/set moveSpeedAccel
    inline const Ogre::Real& getMoveSpeedAccel() const {
        return moveSpeedAccel;
	}
    inline void setMoveSpeedAccel(const Ogre::Real& newMoveSpeedAccel) {
        moveSpeed = newMoveSpeedAccel;
	}

    //get/set rotateSpeed
    inline const Ogre::Degree& getRotateSpeed() const {
        return rotateSpeed;
	}
    inline void setRotateSpeed(const Ogre::Degree& newRotateSpeed) {
        rotateSpeed = newRotateSpeed;
	}

    //get translateVectorAccel
    inline const Ogre::Vector3& getTranslateVectorAccel() const {
        return translateVectorAccel;
	}
    bool getIntersectionPoints(   );
    //get camera
    inline Ogre::Camera* getCamera() const {
        return mCamera;
	}

    bool isCamMovingAtAll() const;

    int updateCameraView();

    bool onFrameStarted   ();
    bool onFrameEnded     ();


    void            moveCamera          (const Ogre::Real frameTime);
    const Ogre::Vector3   getCameraViewTarget ();
    void            onMiniMapClick(Ogre ::Vector2 cc);
    void            flyTo               (const Ogre::Vector3& destination);

    void        move        (const Direction direction, double aux = 0.0);
    inline void stopZooming () {
        zChange = 0;
	}
    inline Ogre::Camera* getCamera() {
        return mCamera;
	}
    private:
    bool switchedPM ;
    Ogre::String switchPolygonMode();

    set<Creature*>*  currentVisibleCreatures;
    set<Creature*>*  previousVisibleCreatures ;


    bool circleMode;
    bool catmullSplineMode;
	
    double radious;
    int centerX;
    int centerY; 
    double alpha;
    




    Ogre::Plane myplanes[6];
    Ogre::Ray myRay[4];

   
    ModeManager* modeManager;
    AbstractApplicationMode* gameMode;
    Ogre::Vector3 ogreVectorsArray[4];
    Vector3i *top, *bottom, *middleLeft, *middleRight;
    Vector3i *oldTop, *oldBottom, *oldMiddleLeft, *oldMiddleRight;
    int precisionDigits;
    
    void sort(Vector3i*& p1 , Vector3i*& p2, bool sortByX);

    // we use the above variables for the methods below

    int bashAndSplashTiles(int); // set the new tiles
    CameraManager(const CameraManager&);

    Ogre::Camera*       mCamera;
    Ogre::SceneNode*    mCamNode;



    GameMap* gameMap;
    bool            cameraIsFlying;
    Ogre::Real      moveSpeed;
    Ogre::Real      moveSpeedAccel;
    Ogre::Real      cameraFlightSpeed;
    Ogre::Degree    rotateSpeed;
    Ogre::Degree    swivelDegrees;
    Ogre::Vector3   translateVector;
    Ogre::Vector3   translateVectorAccel;
    Ogre::Vector3   cameraFlightDestination;
    Ogre::Vector3   mRotateLocalVector;
    double          zChange;
    float           mZoomSpeed;



    set<Creature*> tmpMortuary;


    };

 #endif /* CAMERAMANAGER_H_ */
