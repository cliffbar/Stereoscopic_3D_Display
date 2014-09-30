/*******************************************************************************
Takes the feed simultaneously from two cameras and displays it as a stereoscopic image on a 3D TV

some code adapted from https://www.opengl.org/discussion_boards/showthread.php/181714-Does-opengl-help-in-the-display-of-an-existing-image
useful information from http://www.lighthouse3d.com/tutorials/glut-tutorial/animation/

keyboard hook based on http://stackoverflow.com/questions/22975916/global-keyboard-hook-with-wh-keyboard-ll-and-keybd-event-windows

based also on code provided by Point Grey Research, Inc with the FlyCap2 SDK 

note: uses Windows-specific "timeGetTime" for ms timing accuracy
 threading for saving images is also Windows specific

Cliff Bargar, 8/22/2014

*******************************************************************************/

#include "stdafx.h"
#include <iostream>
#include "FL3Camera.h"


//required libraries are freeglut and the FlyCap SDK:
//http://www.transmissionzero.co.uk/software/freeglut-devel/
//http://www.ptgrey.com/support/downloads/documents/flycapture2/Default.htm, http://ww2.ptgrey.com/sdk/flycap

using namespace FlyCapture2;

//****************DEFINES****************
#define DEFAULT_WIDTH	1280//1920
#define DEFAULT_HEIGHT	720//1080

#define RIGHT_SERIAL	14150448	//current serial number for Right
#define	LEFT_SERIAL		14150447	//serial for Left

#define BASE_LENGTH		12		//number of characters in base filename
#define PATH_DIR		"image_data\\"

#define LOGGING			true	//turns logging of general print statements on/off

//****************VARIABLES****************
int width = DEFAULT_WIDTH;
int height = DEFAULT_HEIGHT;

Camera* leftCam;
Camera* rightCam;

FL3Camera* left;
FL3Camera* right;

char* baseFilename;

FILE* dataFile;
FILE* logFile;

bool saving_on = false; //flag for turning saving on/off

//buffer for image - don't want to waste time reinitializing
unsigned char *image_buffer_l;
unsigned int rows_l, cols_l, stride_l;

unsigned char *image_buffer_r;
unsigned int rows_r, cols_r, stride_r;

GLuint texid;
unsigned int numCameras;

//timing
DWORD startTime;

//****************PROTOTYPES****************
void PrintBuildInfo();
void PrintError(Error);

void display();//redraws images

//callback for keyboard "listener"
LRESULT CALLBACK LowLevelKeyboardProc(_In_  int nCode, _In_  WPARAM wParam, _In_  LPARAM lParam);
//separate thread function to save images to file
DWORD WINAPI SaveImageFile(LPVOID lpThreadParameter);
//allows data to be passed via lpThreadParameter
struct IMAGE_DATA
{
	char* szPathName;
	void* lpBits;
	int w;
	int h;
};


/* Handler for window-repaint event. Called back when the window first appears and
whenever the window needs to be re-painted. */
void display()
{
	char buffer[50];
	//make sure that a new frame has been grabbed by each camera since the last frame was displayed
	if (left->checkNewFrame() && right->checkNewFrame())
	{
		//clear new frame flag
		left->clearNewFrame();
		right->clearNewFrame();

		// Clear color and depth buffers
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glMatrixMode(GL_MODELVIEW);     // Operate on model-view matrix

		//displaying right image
		/* Draw a quad */
		glBegin(GL_QUADS);
		glTexCoord2i(0, 0); glVertex2i(0, 0);
		glTexCoord2i(0, 1); glVertex2i(0, height);
		glTexCoord2i(1, 1); glVertex2i(width / 2, height);
		glTexCoord2i(1, 0); glVertex2i(width / 2, 0);
		glEnd();
		//void glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *data);
		//conceivably renders the image data as a texture
		//from https://www.khronos.org/opengles/sdk/1.1/docs/man/glTexImage2D.xml internalFormat must match format
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, right->getCols(), right->getRows(), 0, GL_RGB, GL_UNSIGNED_BYTE, right->getBuffer()); /* Texture specification */
		//get timestamp for saving in data file
		unsigned long timestampRight = right->getTimestamp();

		//displaying left image
		/* Draw a quad */
		glBegin(GL_QUADS);
		glTexCoord2i(0, 0); glVertex2i(width / 2, 0);
		glTexCoord2i(0, 1); glVertex2i(width / 2, height);
		glTexCoord2i(1, 1); glVertex2i(width, height);
		glTexCoord2i(1, 0); glVertex2i(width, 0);
		glEnd();
		//if there are two cameras display left and right
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, left->getCols(), left->getRows(), 0, GL_RGB, GL_UNSIGNED_BYTE, left->getBuffer());
		//get timestamp for saving in data file
		unsigned long timestampLeft = left->getTimestamp();

		glutSwapBuffers();

		//variables for evaluating display rate
		static unsigned int frameNum = 0;
		static DWORD prevTime = 0;
		static double net_fps = 0, prev_fps = 0;
		DWORD currentTime = timeGetTime(); //"retrieves the system time, in milliseconds. The system time is the time elapsed since Windows was started."

		//only saves images if the save toggle variable is turned on - toggled by pressing 'R' (for "record")
		if (saving_on)
		{
			//Save image
			static int dataSize = 3 * glutGet(GLUT_WINDOW_WIDTH) * glutGet(GLUT_WINDOW_HEIGHT);
			static char* pbyData = (char*)malloc(dataSize);

			//checking if window size changed
			if (dataSize != 3 * glutGet(GLUT_WINDOW_WIDTH) * glutGet(GLUT_WINDOW_HEIGHT))
			{
				//reallocate pbyData if necessary
				dataSize = 3 * glutGet(GLUT_WINDOW_WIDTH) * glutGet(GLUT_WINDOW_HEIGHT);
				delete pbyData;
				pbyData = (char*)malloc(dataSize);
			}

			//save current screen into buffer pbyData
			glReadPixels(0, 0, glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT), GL_BGR_EXT, GL_UNSIGNED_BYTE, pbyData);

			//assemble data required for saving bmp
			static IMAGE_DATA* save_data = (IMAGE_DATA*)malloc(sizeof(IMAGE_DATA*));
			save_data->lpBits = pbyData;
			save_data->w = glutGet(GLUT_WINDOW_WIDTH);
			save_data->h = glutGet(GLUT_WINDOW_HEIGHT);
			//create file name based on frame number and time of execution
			save_data->szPathName = (char*)malloc(100 * sizeof(char)); //note: I'm not sure if this is a memory leak
			sprintf(save_data->szPathName, "%s\\%s-%i.bmp", baseFilename, baseFilename, frameNum);

			static HANDLE prevThreadHandle = 0;
			//waits for previous image to save before starting to save this one
			if (prevThreadHandle != 0)
			{
				WaitForSingleObject(prevThreadHandle, INFINITE);
			}
			//create a new thread to save the image
			prevThreadHandle = CreateThread(NULL, 0, SaveImageFile, save_data, 0, NULL);
		}

		//calculate display rate
		double fps = 1 / ((double)(currentTime - prevTime)/1000);
		//calculate display rate with a low pass filter
		if ((prevTime != 0) & (prev_fps != 0))
		{
			net_fps = 0.65 * fps + 0.2 * prev_fps + 0.15 * net_fps;
		}
		sprintf(buffer, "Displaying frame (%i): display rate %f\n", frameNum, net_fps);
		printf(buffer);
		if (LOGGING)
		{
			fwrite(buffer, sizeof(char), strlen(buffer), logFile);
		}

		//save data to file: columns seperated by commas and tabs
		//frame number - timestamp (displayed) - timestamp (left) - timestamp (right)
		sprintf(buffer, "%u,\t%d,\t%d,\t%d\n", frameNum, (currentTime - startTime),timestampLeft,timestampRight);
		fwrite(buffer, sizeof(char), strlen(buffer), dataFile);

		prev_fps = fps;
		prevTime = currentTime;

		frameNum++;
	}
}

/* Initialize OpenGL Graphics */
void initGL(int w, int h, int argc, char **argv)
{
	/* GLUT init */
	glutInit(&argc, argv);            // Initialize GLUT
	glutInitDisplayMode(GLUT_DOUBLE); // Enable double buffered mode
	glutInitWindowSize(DEFAULT_WIDTH, DEFAULT_HEIGHT);   // Set the window's initial width & height
	glutCreateWindow("Raven Stereoscopic 3D");      // Create window 
	glutDisplayFunc(display);       // Register callback handler for window re-paint event
	//glutReshapeFunc(reshape);       // Register callback handler for window re-size event// here is the idle func registration
	glutIdleFunc(display);			//just update display when idle

	/* OpenGL 2D generic init */
	glViewport(0, 0, w, h); // use a screen size of WIDTH x HEIGHT
	glEnable(GL_TEXTURE_2D);     // Enable 2D texturing

	glMatrixMode(GL_PROJECTION);     // Make a simple 2D projection on the entire window
	glLoadIdentity();
	glOrtho(0.0, w, h, 0.0, 0.0, 100.0);

	glMatrixMode(GL_MODELVIEW);    // Set the matrix mode to object modeling

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Clear the window

	/* OpenGL texture binding of the image  */
	glGenTextures(1, &texid); /* Texture name generation */
	glBindTexture(GL_TEXTURE_2D, texid); /* Binding of texture name */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); /* We will use linear interpolation for magnification filter */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); /* We will use linear interpolation for minifying filter */
}

//adapted from pointgrey code
void PrintBuildInfo()
{
    FC2Version fc2Version;
    Utilities::GetLibraryVersion( &fc2Version );
    char version[128];
    sprintf( 
        version, 
        "FlyCapture2 library version: %d.%d.%d.%d\n", 
        fc2Version.major, fc2Version.minor, fc2Version.type, fc2Version.build );

    printf( version );

    char timeStamp[512];
    sprintf( timeStamp, "Application build date: %s %s\n\n", __DATE__, __TIME__ );

    printf( timeStamp );
}


void PrintError( Error error )
{
    error.PrintErrorTrace();
}

int main(int argc, char **argv)
{    
	char buffer[100];
    PrintBuildInfo();

	//get time for counting from beginning of program
	startTime = timeGetTime();

	//creates a base file name based on the month, date, and time that execution began
	std::time_t result = std::time(NULL);
	baseFilename = (char*)malloc(BASE_LENGTH);
	std::strftime(baseFilename, BASE_LENGTH, "%m%d-%H%M%S", std::localtime(&result)); //converts current date/time into timestamp for filename

	//create file name and path for timestamp data file
	char* dataFilename = (char*)malloc(BASE_LENGTH * 2 + 10);
	sprintf(dataFilename, "%s\\%s_data.txt", baseFilename, baseFilename);
	printf("Data will be saved in %s \n", dataFilename);

	char* logFilename = (char*)malloc(BASE_LENGTH * 2 + 9);
	sprintf(logFilename, "%s\\%s_log.txt", baseFilename, baseFilename);

    Error error;

	//Uses the bus manager to find attached cameras
    BusManager busMgr;
    error = busMgr.GetNumOfCameras(&numCameras);
    if (error != PGRERROR_OK)
    {
        PrintError( error );
        return -1;
    }

	//turn logging on/off using define
	if (LOGGING)
	{
		//open log file if serial logging is turned on
		CreateDirectoryA(baseFilename, NULL);
		logFile = fopen(logFilename, "w+");
	}
	else
	{
		logFile = (FILE*)0;
	}

	sprintf(buffer,"Number of cameras detected: %u\n", numCameras);
	printf(buffer);
	if (LOGGING)
	{
		fwrite(buffer, sizeof(char), strlen(buffer), logFile);
	}

	//make sure there are enough cameras; otherwise don't do anything
	if (numCameras >= 2)
	{
		//create directory for saving data and image files
		CreateDirectoryA(baseFilename, NULL); 

		//create text file with timestamp data for displayed frame and corresponding left/right image timestamp
		dataFile = fopen(dataFilename, "w+");

		printf("Ready to begin. Press enter to continue.\nOnce running:\n-press 'F' to toggle fullscreen \n-press Esc to exit \n-press 'R' to toggle recording (saving frames)\n-press the up/down arrows to adjust stereo image spacing\nNote: avoid resizing while recording\n");
		getchar();

		//OpenGL initialization - adapted from openGL tutorial on lighthouse3d
		initGL(DEFAULT_WIDTH, DEFAULT_HEIGHT, argc, argv);

		PGRGuid guid;

		//****connect the cameras:****
		//figure out which camera is which based on serial #
		error = busMgr.GetCameraFromIndex(0, &guid);
		if (error != PGRERROR_OK)
		{
			PrintError(error);
			sprintf(buffer,"Couldn't connect left camera");
			printf(buffer);
			if (LOGGING)
			{
				fwrite(buffer, sizeof(char), strlen(buffer), logFile);
			}
			return -1;
		}

		//need temporary camera object to establish serial number - used for assigning left/right
		Camera tempcam;
		tempcam.Connect(&guid);

		CameraInfo camInfo;
		error = tempcam.GetCameraInfo(&camInfo);
		if (error != PGRERROR_OK)
		{
			error.PrintErrorTrace();
			return -1;
		}

		//if this ID corresponds to the left serial connect the left followed by right, otherwise connect the right followed by left
		if (camInfo.serialNumber == LEFT_SERIAL)
		{
			//connect left
			left = new FL3Camera(CAMERA_NAME_LEFT, startTime, logFile);
			left->connect(guid);

			//connect right camera
			error = busMgr.GetCameraFromIndex(1, &guid);
			if (error != PGRERROR_OK)
			{
				PrintError(error);
				sprintf(buffer,"Couldn't connect right camera");
				printf(buffer);
				if (LOGGING)
				{
					fwrite(buffer, sizeof(char), strlen(buffer), logFile);
				}
				return -1;
			}

			right = new FL3Camera(CAMERA_NAME_RIGHT, startTime, logFile);
			right->connect(guid);
		}
		else
		{
			//connect right camera
			right = new FL3Camera(CAMERA_NAME_RIGHT, startTime, logFile);
			right->connect(guid);

			//left camera
			error = busMgr.GetCameraFromIndex(1, &guid);
			if (error != PGRERROR_OK)
			{
				PrintError(error);
				printf("Couldn't connect right camera");
				return -1;
			}

			left = new FL3Camera(CAMERA_NAME_LEFT, startTime, logFile);
			left->connect(guid);
		}

		//****start capture****
		left->start();
		right->start();


		//***run OpenGL***
		//make sure glutMainLoop returns when glutLeaveMainLoop() is called
		glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_CONTINUE_EXECUTION);

		//install keyboard hook
		HHOOK hhkLowLevelKybd = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, 0, 0);

		//runs continuously until Esc is pressed (in keyboard hook)
		glutMainLoop();
		sprintf(buffer,"Exited main loop\n");
		printf(buffer);
		if (LOGGING)
		{
			fwrite(buffer, sizeof(char), strlen(buffer), logFile);
		}

		//****disconnect cameras when glut ceases ****
		left->disconnectCamera();
		right->disconnectCamera();

		//remove keyboard hook
		UnhookWindowsHookEx(hhkLowLevelKybd);

		//close data file
		fclose(dataFile);
	}
	else
	{
		sprintf(buffer,"Too few cameras connected\n");
		printf(buffer);
		if (LOGGING)
		{
			fwrite(buffer, sizeof(char), strlen(buffer), logFile);
		}
	}
	if (LOGGING)
		fclose(logFile);
   
	printf( "Done! Press Enter to exit...\n" );
    getchar();


    return 0;
}

//callback for key press hook
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	char buffer[50];
	if (nCode == HC_ACTION)
	{
		switch (wParam)
		{
		case WM_KEYDOWN:
			PKBDLLHOOKSTRUCT p = (PKBDLLHOOKSTRUCT)lParam;
			//leave main loop (end program, disconnect from cameras) if Esc was pressed
			if (p->vkCode == VK_ESCAPE)
			{
				sprintf(buffer,"Esc pressed --> leave main loop\n");
				printf(buffer);
				if (LOGGING)
				{
					fwrite(buffer, sizeof(char), strlen(buffer), logFile);
				}
				glutLeaveMainLoop();
				return 1;
			}
			//toggle between full screen and windowed on spacebar
			if (p->vkCode == 'F')
			{
				sprintf(buffer,"F --> toggle fullscreen\n");
				printf(buffer);
				if (LOGGING)
				{
					fwrite(buffer, sizeof(char), strlen(buffer), logFile);
				}
				glutFullScreenToggle();
				return 1;
			}
			//toggles recording on/off
			if (p->vkCode == 'R')
			{
				sprintf(buffer,"R --> toggle recording\n");
				printf(buffer);
				if (LOGGING)
				{
					fwrite(buffer, sizeof(char), strlen(buffer), logFile);
				}
				saving_on = !saving_on;
				return 1;
			}
			//use up/down arrows to adjust the stereo spacing between left and right frames
			if (p->vkCode == VK_UP)
			{
				sprintf(buffer, "Up pressed --> increase stereo spacing\n");
				printf(buffer);
				if (LOGGING)
				{
					fwrite(buffer, sizeof(char), strlen(buffer), logFile);
				}
				left->increaseOffset();
				right->increaseOffset();
				return 1;
			}
			if (p->vkCode == VK_DOWN)
			{
				sprintf(buffer, "DOWN pressed --> decrease stereo spacing\n");
				printf(buffer);
				if (LOGGING)
				{
					fwrite(buffer, sizeof(char), strlen(buffer), logFile);
				}
				left->decreaseOffset();
				right->decreaseOffset();
				return 1;
			}
			break;
		}
	}
	return(CallNextHookEx(NULL, nCode, wParam, lParam));//fEatKeystroke ? 1 : CallNextHookEx(NULL, nCode, wParam, lParam));
}


// szPathName : Specifies the pathname
// lpBits	 : Specifies the bitmap bits
// w	: Specifies the image width
// h	: Specifies the image height
//this based on http://www.codeproject.com/Tips/348048/Save-a-24-bit-bitmaps-pixel-data-to-File-in-BMP-fo#
//license http://www.codeproject.com/info/cpol10.aspx
DWORD WINAPI SaveImageFile(LPVOID lpThreadParameter)//char* szPathName, void* lpBits, int w, int h
{
	IMAGE_DATA* data = (IMAGE_DATA*) lpThreadParameter;
	//printf("Saving to %s\n", szPathName);

	//Create a new file for writing
	FILE *pFile = fopen(data->szPathName, "wb");
	if (pFile == NULL)
	{
		printf("Failed\n");
		return false;
	}
	BITMAPINFOHEADER BMIH;
	BMIH.biSize = sizeof(BITMAPINFOHEADER);
	BMIH.biSizeImage = data->w * data->h * 3;

	// Create the bitmap for this OpenGL context
	BMIH.biSize = sizeof(BITMAPINFOHEADER);
	BMIH.biWidth = data->w;
	BMIH.biHeight = data->h;
	BMIH.biPlanes = 1;
	BMIH.biBitCount = 24;
	BMIH.biCompression = BI_RGB;
	BMIH.biSizeImage = data->w * data->h * 3;

	BITMAPFILEHEADER bmfh;

	int nBitsOffset = sizeof(BITMAPFILEHEADER) + BMIH.biSize;

	LONG lImageSize = BMIH.biSizeImage;
	LONG lFileSize = nBitsOffset + lImageSize;

	bmfh.bfType = 'B' + ('M' << 8);
	bmfh.bfOffBits = nBitsOffset;
	bmfh.bfSize = lFileSize;
	bmfh.bfReserved1 = bmfh.bfReserved2 = 0;

	//Write the bitmap file header
	fwrite(&bmfh, 1, sizeof(BITMAPFILEHEADER), pFile);

	//And then the bitmap info header
	fwrite(&BMIH,1, sizeof(BITMAPINFOHEADER), pFile);

	//Finally, write the image data itself 
	//-- the data represents our drawing
	fwrite(data->lpBits, 1, lImageSize, pFile);

	fclose(pFile);

	return 0;
}
