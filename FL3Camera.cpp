//Class for controlling PointGrey Flea3 cameras for use in stereo vision applications
//Stanford CHARM Lab, NRI project
//Cliff Bargar
//8/22/14

// ============================================================================

#include "stdafx.h"
#include "FL3Camera.h"

//defines for image capture, etc
#define		IMAGE_WIDTH		1280 //default width to capture
#define		IMAGE_HEIGHT	720  //default height to capture
//ROI offset: based off prior calculation of ~18.6% between the cameras and width of 1280
//400 is from FlyCap, centering the ROI
#define		OFFSET_RIGHT_DEF	288	 //400 - 238/2 but divisible by 4
#define		OFFSET_LEFT_DEF		512  //400 + 238/2 but divisible by 4
#define		OFFSET_VERTICAL		416	 //from FlyCap


// ============================================================================
//public functions
FL3Camera::FL3Camera()
{
	cameraName = "default";
	bufferInitialized = false;
	frameNum = 0;
	startTime = 0;
	prevTime= 0;
	net_fps = 0;
	prev_fps = 0;
	acqInProgress = false;
	newFrame = false;
	logFile = 0;
}

FL3Camera::FL3Camera(std::string name, DWORD start, FILE* log)
{
	cameraName = name;
	bufferInitialized = false;
	frameNum = 0;
	startTime = start;
	prevTime = 0;
	net_fps = 0;
	prev_fps = 0;
	acqInProgress = false;
	newFrame = false; 
	logFile = log;
}
// ----------------------------------------------------------------------------


FL3Camera::~FL3Camera()
{
	delete cam;
	delete image_buffer;
}
// ----------------------------------------------------------------------------

void FL3Camera::increaseOffset()
{
	if (cameraName.compare(CAMERA_NAME_LEFT) == 0)
	{
		//shift left image to the right
		offset = offset + 4;
	}
	else
	{
		//shift right image to the left
		offset = offset - 4;
	}
	
	fmt7ImageSettings.offsetX = offset;

	cam->StopCapture();
	cam->SetFormat7Configuration(
		&fmt7ImageSettings,
		fmt7PacketInfo.recommendedBytesPerPacket);
	cam->StartCapture(callGrabFrame, this);

}
// ----------------------------------------------------------------------------

void FL3Camera::decreaseOffset()
{
	if (cameraName.compare(CAMERA_NAME_LEFT) == 0)
	{
		//shift left image to the left
		offset = offset - 4;
	}
	else
	{
		//shift right image to the right
		offset = offset + 4;
	}
	fmt7ImageSettings.offsetX = offset;

	cam->StopCapture();
	cam->SetFormat7Configuration(
		&fmt7ImageSettings,
		fmt7PacketInfo.recommendedBytesPerPacket);
	cam->StartCapture(callGrabFrame, this);

}
// ----------------------------------------------------------------------------

void FL3Camera::connect(PGRGuid guid)
{
	cam_id = guid;
	//creates new camera object
	cam = new Camera;

	//connect to the camera
	connectCamera(cam_id, cam);

	char buffer[50];
	sprintf(buffer,"Connected %s camera\n", cameraName.c_str());
	printf(buffer);
	if (logFile != 0)
	{
		fwrite(buffer, sizeof(char), strlen(buffer), logFile);
	}

	//initialize
	Error error;

	//grab a single frame to check how much memory is needed for buffer
	cam->StartCapture();
	Image rawImage;
	error = cam->RetrieveBuffer(&rawImage);
	cam->StopCapture();

	if (error != PGRERROR_OK)
		error.PrintErrorTrace();
	// Convert the raw image to RGB format
	error = rawImage.Convert(PIXEL_FORMAT_RGB, &convertedImage);
	if (error != PGRERROR_OK)
		error.PrintErrorTrace();

	//initialize buffer
	if (!bufferInitialized) 
	{
		sprintf(buffer,"Initializing buffer\n");
		printf(buffer);
		if (logFile != 0)
		{
			fwrite(buffer, sizeof(char), strlen(buffer), logFile);
		}
		image_buffer = new unsigned char[convertedImage.GetDataSize()];
		bufferInitialized = true;
	}
}
// ----------------------------------------------------------------------------
void FL3Camera::start()
{
	char buffer[50];
	cam->StartCapture(callGrabFrame, this);

	sprintf(buffer,"Started  %s camera\n", cameraName.c_str());
	printf(buffer);
	if (logFile != 0)
	{
		fwrite(buffer, sizeof(char), strlen(buffer), logFile);
	}
}
// ----------------------------------------------------------------------------
int FL3Camera::disconnectCamera()
{
	char buffer[50];
	Error error;

	// Stop capturing images
	error = cam->StopCapture();
	if (error != PGRERROR_OK)
	{
		error.PrintErrorTrace();
		return -1;
	}

	// Disconnect the camera
	error = cam->Disconnect();
	if (error != PGRERROR_OK)
	{
		error.PrintErrorTrace();
		return -1;
	}

	sprintf(buffer,"Disconnected %s camera\n", cameraName.c_str());
	printf(buffer);
	if (logFile != 0)
	{
		fwrite(buffer, sizeof(char), strlen(buffer), logFile);
	}
	return 0;

}
// ----------------------------------------------------------------------------

//grabFrame is called by the callback function indirectly: the Image* is new data, the other array is various data
void FL3Camera::grabFrame(Image* pImage)
{
	char buffer[100];
	//set acquisition flag to true
	//can be used to prevent image being displayed mid-frame
	acqInProgress = true;

	Error error;
	if (startTime == 0)
		startTime = timeGetTime();
	currentTime = timeGetTime();

	// Convert the raw image to RGB format
	error = pImage->Convert(PIXEL_FORMAT_RGB, &convertedImage);
	if (error != PGRERROR_OK)
	{
		error.PrintErrorTrace();
	}

	//get the necessary dimensions
	PixelFormat pixFormat;
	convertedImage.GetDimensions(&rows, &cols, &stride, &pixFormat);

	//"current" FPS value for just this frame - put in low pass filter
	double fps = 1 / ((double)(currentTime - prevTime) / 1000);

	//calculate FPS with a low pass filter
	if ((prevTime != 0) & (prev_fps !=0))
	{
		net_fps = 0.65 * fps + 0.2 * prev_fps + 0.15 * net_fps;
	}
	sprintf(buffer,"New %s frame (%d) at %f; %f fps\n", cameraName.c_str(), frameNum, (double)(currentTime - startTime)/1000,  net_fps);
	printf(buffer);
	if (logFile != 0)
	{
		fwrite(buffer, sizeof(char), strlen(buffer), logFile);
	}
	prev_fps = fps;
	prevTime = currentTime;

	//copy image to buffer
	memcpy(image_buffer, convertedImage.GetData(), convertedImage.GetDataSize());

	//frame is fully acquired
	acqInProgress = false;
	//flag indicating a new frame is available to be displayed
	newFrame = true;


	frameNum++;
}
// ----------------------------------------------------------------------------

//callGrabFrame is called by the callback function; pCallbackData is a pointer to the FL3Camera object whose grabFrame function should be called
static void callGrabFrame(Image* pImage, const void* pCallbackData)
{
	((FL3Camera*)pCallbackData)->grabFrame(pImage);

}
// ----------------------------------------------------------------------------

//returns pointer to buffer of current image data that should be displayed
unsigned char* FL3Camera::getBuffer()
{

	return image_buffer;
}

unsigned int FL3Camera::getCols()
{
	return cols;
}
unsigned int FL3Camera::getRows()
{
	return rows;
}

unsigned long FL3Camera::getTimestamp()
{
	return currentTime - startTime;
}

bool FL3Camera::checkNewFrame()
{
	return newFrame;
}

void FL3Camera::clearNewFrame()
{
	newFrame = false;
}

// ============================================================================
//private functions

int FL3Camera::connectCamera(PGRGuid guid, Camera* cam)
{
	char buffer[50];
	//adapted from CustomImageEx example --> setting resolution and ROI
	const Mode k_fmt7Mode = MODE_8;
	const PixelFormat k_fmt7PixFmt = PIXEL_FORMAT_RAW8;

	Error error;

	// Connect to a camera
	error = cam->Connect(&guid);
	if (error != PGRERROR_OK)
	{
		error.PrintErrorTrace();
		return -1;
	}

	// Get the camera information
	CameraInfo camInfo;
	error = cam->GetCameraInfo(&camInfo);
	if (error != PGRERROR_OK)
	{
		error.PrintErrorTrace();
		return -1;
	}

	PrintCameraInfo(&camInfo);

	// Query for available Format 8 modes
	Format7Info fmt7Info;
	bool supported;
	fmt7Info.mode = k_fmt7Mode;
	error = cam->GetFormat7Info(&fmt7Info, &supported);
	if (error != PGRERROR_OK)
	{
		error.PrintErrorTrace();
		return -1;
	}

	PrintFormat7Capabilities(fmt7Info);

	fmt7ImageSettings.mode = k_fmt7Mode;
	//if this is the left camera, do the left offset
	if (cameraName.compare(CAMERA_NAME_LEFT) == 0)
	{
		offset = OFFSET_LEFT_DEF;
		fmt7ImageSettings.offsetX = offset;
		sprintf(buffer,"Left camera offset \n");
		printf(buffer);
		if (logFile != 0)
		{
			fwrite(buffer, sizeof(char), strlen(buffer), logFile);
		}
	}
	//otherwise assume right camera
	else
	{
		offset = OFFSET_RIGHT_DEF;
		fmt7ImageSettings.offsetX = offset;
		sprintf(buffer,"Right camera offset \n");
		printf(buffer);
		if (logFile != 0)
		{
			fwrite(buffer, sizeof(char), strlen(buffer), logFile);
		}
	}
		
	fmt7ImageSettings.offsetY = OFFSET_VERTICAL;
	fmt7ImageSettings.width = IMAGE_WIDTH;
	fmt7ImageSettings.height = IMAGE_HEIGHT;//
	fmt7ImageSettings.pixelFormat = k_fmt7PixFmt;

	bool valid;

	// Validate the settings to make sure that they are valid
	error = cam->ValidateFormat7Settings(
		&fmt7ImageSettings,
		&valid,
		&fmt7PacketInfo);
	if (error != PGRERROR_OK)
	{
		error.PrintErrorTrace();
		return -1;
	}

	if (!valid)
	{
		// Settings are not valid
		sprintf(buffer,"Format7 settings are not valid\n");
		printf(buffer);
		if (logFile != 0)
		{
			fwrite(buffer, sizeof(char), strlen(buffer), logFile);
		}
		return -1;
	}

	// Set the settings to the camera
	error = cam->SetFormat7Configuration(
		&fmt7ImageSettings,
		fmt7PacketInfo.recommendedBytesPerPacket);
	if (error != PGRERROR_OK)
	{
		error.PrintErrorTrace();
		return -1;
	}


	return 0;
}
// ----------------------------------------------------------------------------

void FL3Camera::PrintCameraInfo(CameraInfo* pCamInfo)
{
	char buffer[500];
	sprintf(buffer,
		"\n*** CAMERA INFORMATION ***\n"
		"Serial number - %u\n"
		"Camera model - %s\n"
		"Camera vendor - %s\n"
		"Sensor - %s\n"
		"Resolution - %s\n"
		"Firmware version - %s\n"
		"Firmware build time - %s\n\n",
		pCamInfo->serialNumber,
		pCamInfo->modelName,
		pCamInfo->vendorName,
		pCamInfo->sensorInfo,
		pCamInfo->sensorResolution,
		pCamInfo->firmwareVersion,
		pCamInfo->firmwareBuildTime);
	printf(buffer);
	if (logFile != 0)
	{
		fwrite(buffer, sizeof(char), strlen(buffer), logFile);
	}
}

void FL3Camera::PrintFormat7Capabilities(Format7Info fmt7Info)
{
	char buffer[500];
	sprintf(buffer,
		"Max image pixels: (%u, %u)\n"
		"Image Unit size: (%u, %u)\n"
		"Offset Unit size: (%u, %u)\n"
		"Pixel format bitfield: 0x%08x\n",
		fmt7Info.maxWidth,
		fmt7Info.maxHeight,
		fmt7Info.imageHStepSize,
		fmt7Info.imageVStepSize,
		fmt7Info.offsetHStepSize,
		fmt7Info.offsetVStepSize,
		fmt7Info.pixelFormatBitField);
	printf(buffer);
	if (logFile != 0)
	{
		fwrite(buffer, sizeof(char), strlen(buffer), logFile);
	}
}