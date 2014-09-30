#ifndef FL3_CAMERA
#define FL3_CAMERA
// ============================================================================

//Class for controlling PointGrey Flea3 cameras for use in stereo vision applications
//Stanford CHARM Lab, NRI project
//Cliff Bargar
//8/22/14

// ============================================================================
//#pragma once

#include "FlyCapture2.h"
#include <GL/freeglut.h>
#include <ctime>  
#include <string>
#include <mmsystem.h>

#define		CAMERA_NAME_LEFT	"Left"  //string for name of left camera
#define		CAMERA_NAME_RIGHT	"Right" //string for name of left camera

using namespace FlyCapture2;

// ============================================================================

class FL3Camera
{
public:
	FL3Camera();
	FL3Camera(std::string, DWORD, FILE*);
	~FL3Camera();
	
	void connect(PGRGuid);
	void start();

	int disconnectCamera();

	//returns value of newFrame flag
	bool checkNewFrame(); 
	//clears the newFrame flag
	void clearNewFrame(); 

	//grabFrame is called indirectly by the callback function: the Image* is new data
	void grabFrame(Image*);

	//returns buffer of current image data that should be displayed
	unsigned char* getBuffer();
	//returns width in pixels of current frame
	unsigned int getCols();
	//returns height in pixels of current frame
	unsigned int getRows();
	//returns timestamp in ms (since start of execution) of current frame
	unsigned long getTimestamp();

	//increases offset between images
	void increaseOffset();
	//decreases offset between images
	void decreaseOffset();

private:
	//data
	Camera* cam;
	unsigned char* image_buffer;
	unsigned int cols, rows, stride;
	PGRGuid cam_id;
	std::string cameraName;
	bool bufferInitialized;
	int frameNum;

	//for adjusting stereoscopic effect
	unsigned int offset;
	Format7ImageSettings fmt7ImageSettings;
	Format7PacketInfo fmt7PacketInfo;

	//for calculating framerate
	DWORD startTime;			//time first frame was captured
	DWORD prevTime;			//time previous frame was captured - for FPS calculation
	double net_fps, prev_fps;	//variables for calculating FPS through low pass filter
	DWORD currentTime;		//timestamp for current frame

	FILE* logFile;		//pointer to file for saving print statements


	//to hold the newly converted image each timestep
	Image convertedImage;
	//flag for image being aquired
	bool acqInProgress;
	//flag for new image to display
	bool newFrame;

	//private prototypes
	int connectCamera(FlyCapture2::PGRGuid, FlyCapture2::Camera*);
	void PrintCameraInfo(FlyCapture2::CameraInfo*);
	void PrintFormat7Capabilities(Format7Info);

};

//function summoned by callback
static void callGrabFrame(Image* pImage, const void* pCallbackData);

// ============================================================================
#endif

