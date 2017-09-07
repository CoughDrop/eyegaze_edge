// EdgeTracker.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "EgWin.h"
#include <time.h>
#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include <csignal>
#include <future>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <Windows.h>

#pragma comment (lib, "lctigaze.lib")

#define  TRUE           1       /* Self explanatory                         */
#define  FALSE          0       /*                                          */
#define  EVER          ;;       /* used in infinite for loop                */

int      iScreenWidth;             /* pixel dimensions of the full          */
int      iScreenHeight;            /*   computer screen                     */

								   /* Demonstration Program variables:                                         */
int iIGazeSmooth;             /* smoothed values of the eyegaze          */
							  /*   coordinates (pixels)                  */
int iJGazeSmooth;
int iVis;                     /* The vision system returning data        */
int rc;                       /* Return code from EgInit function call   */

static struct _stEgControl stEgControl;
/* The eyetracking application must define */
/*   (and fill in) this Eyegaze control    */
/*   structure                             */
/*   (See structure template in EgWin.h)   */

/***************************************************************************/
void SmoothGazepoint(int iEyeFound,
	int iIGaze, int iJGaze,
	int *iIGazeSmooth, int *iJGazeSmooth,
	int iNSmoothPoints)

	/* This function smooths the gazepoint by averaging the all the valid       */
	/* gazepoints within the last iNSmoothPoints.                               */

{
#define SMOOTH_BUF_LEN   60

	static int  iIGazeSave[SMOOTH_BUF_LEN];    /* past gazepoint I buffer          */
	static int  iJGazeSave[SMOOTH_BUF_LEN];    /* past gazepoint J buffer          */
	static int  iEyeFoundSave[SMOOTH_BUF_LEN]; /* buffer of past eye-found flags   */
	static int  iBufIndex;                     /* buffer index of the present      */
											   /*   gazepoint                      */
											   /*   -- varies from 0 to            */
											   /*      N_SMOOTH_BUF_LEN            */
	int   iIGazeSum;                     /* gazepoint summations used for    */
	int   iJGazeSum;                     /*   averaging                      */
	int   i;                             /* past sample index                */
	int   n;                             /* buffer index                     */
	int   iNAvgPoints;                   /* number of actual gazepoints      */
										 /*   averaged                       */

										 /* Make sure the number of points we're asked to smooth is at least one     */
										 /* but not more than the smooth buffer size.                                */
	if (iNSmoothPoints < 1)             iNSmoothPoints = 1;
	if (iNSmoothPoints > SMOOTH_BUF_LEN)  iNSmoothPoints = SMOOTH_BUF_LEN;

	/* Increment the buffer index for the new gazepoint.                        */
	iBufIndex++;
	if (iBufIndex >= SMOOTH_BUF_LEN) iBufIndex = 0;

	/* Record the newest gazepoint in the buffer.                               */
	iEyeFoundSave[iBufIndex] = iEyeFound;
	iIGazeSave[iBufIndex] = iIGaze;
	iJGazeSave[iBufIndex] = iJGaze;

	/* Initialize the average summations.                                       */
	iNAvgPoints = 0;
	iIGazeSum = 0;
	iJGazeSum = 0;

	/* Loop through the past iNSmoothPoints.                                    */
	for (i = 0; i < iNSmoothPoints; i++)
	{
		/*    Find the buffer index of the ith prior point.                         */
		n = iBufIndex - i;
		if (n < 0) n += SMOOTH_BUF_LEN;

		/*    If the eye was found on the ith prior sample,                         */
		if (iEyeFoundSave[n] == TRUE)
		{
			/*       Accumulate the average summations.                                 */
			iNAvgPoints++;
			iIGazeSum += iIGazeSave[n];
			iJGazeSum += iJGazeSave[n];
		}
	}

	/* If there were one or more valid gazepoints during the last               */
	/* iNSmoothPoints,                                                          */
	if (iNAvgPoints > 0)
	{
		/*    Set the smoothed gazepoint to the average of the valid gazepoints     */
		/*    collected during that period.                                         */
		*iIGazeSmooth = iIGazeSum / iNAvgPoints;
		*iJGazeSmooth = iJGazeSum / iNAvgPoints;
	}

	/* Otherwise, if no valid gazepoints were collected during the last         */
	/* iNSmoothPoints, leave the smoothed gazepoint alone.                      */
	/* (no code)                                                                */
}

bool keep_polling = true;
std::mutex m;
std::condition_variable cv;

void signalHandler(int signum) {
	printf("Manually exited\n");
	std::streambuf* orig = std::cin.rdbuf();
	std::istringstream input("q\n");
	std::cin.rdbuf(input.rdbuf());
	// tests go here
	std::cin.rdbuf(orig);

	auto lock = std::unique_lock<std::mutex>(m);
	lock.unlock();
	cv.notify_all();

	keep_polling = false;

//	EgExit(&stEgControl);

	exit(signum);
}
int main(int argc, char *argv[])
{
	printf("Edge Tracker\n");
	signal(SIGINT, signalHandler);

//	HDC screen = GetDC(0);
//	int dpiX = GetDeviceCaps(screen, LOGPIXELSX);
//	int dpiY = GetDeviceCaps(screen, LOGPIXELSY);
//	ReleaseDC(0, screen);

//	printf("%i %i logpixels\n", dpiX, dpiY);
	iScreenWidth = GetSystemMetrics(SM_CXSCREEN);
	iScreenHeight = GetSystemMetrics(SM_CYSCREEN);

#define EG_BUFFER_LEN 60      /* The constant EG_BUFFER_LEN sets the     */
	/*   number of past samples stored in      */
	/*   its gazepoint data ring buffer.       */
	/*   Assuming an Eyegaze sample rate of    */
	/*   60 Hz, the value 60 means that one    */
	/*   second's worth of past Eyegaze data   */
	/*   is always available in the buffer.    */
	/*   The application can get up to 60      */
	/*   samples behind the Eyegaze image      */
	/*   processing without losing eyetracking */
	/*   data.                                 */


	/*---------------------------- Function Code -------------------------------*/

	/* Calibrate to the Eyegaze user (test subject).                            */
	/* Since an Eyegaze thread has not yet been started, execute the external   */
	/* Eyegaze Calibration program (CALIBRATE.EXE), which starts and terminates */
	/* its own Eyegaze thread.                                                  */
	/* (See discussion of alternative calibration calls in the Eyegaze System   */
	/* Programmer's Manual. NOTE: If using the double computer configuration,   */
	/* spawning the external CALIBRATE.EXE is not an option.  EgCalibrate must  */
	/* be used.  See Programmer's Manual)                                       */
	/* P_WAIT causes the calibration program to terminate before this program   */
	/* proceeds.                                                                */
	//_spawnl(P_WAIT, "Calibrate.exe", "Calibrate.exe", NULL);

	/* Set the input control constants in stEgControl required for starting     */
	/* the Eyegaze thread.                                                      */
	stEgControl.iNDataSetsInRingBuffer = EG_BUFFER_LEN;
	/* Tell Eyegaze the length of the Eyegaze  */
	/*   data ring buffer                      */
	stEgControl.bTrackingActive = FALSE;
	/* Tell Eyegaze not to begin image         */
	/*   processing yet (so no past gazepoint  */
	/*   data samples will have accumulated    */
	/*   in the ring buffer when the tracking  */
	/*   loop begins).                         */
	stEgControl.iScreenWidthPix = iScreenWidth;
	stEgControl.iScreenHeightPix = iScreenHeight;
	/* Tell the image processing software what */
	/*   the physical screen dimensions are    */
	/*   in pixels.                            */

	stEgControl.iCommType = EG_COMM_TYPE_LOCAL;

	/* Create the Eyegaze image processing thread.                              */
	rc = EgInit(&stEgControl);
	bool do_poll = true;
	if (rc != 0)
	{
		printf("Error %i Initializing Eyegaze Edge\n", rc);
		do_poll = false;
	}
	else {
		printf("Eyegaze Edge initialized!\n");
		if(argc > 1 && std::string(argv[1]) == "calibrate") {
			EgCalibrate2(&stEgControl, EG_CALIBRATE_DISABILITY_APP);
			printf("Eyegaze Edge calibrated\n");
			do_poll = false;
		}
	}

	/* Tell Eyegaze to start eyetracking, i.e. to begin image processing.       */
	/* Note: No eyetracking takes place until this flag is set to true, and     */
	/* eye image processing stops when this flag is reset to false.             */
	stEgControl.bTrackingActive = TRUE;


	std::string new_string;
	bool new_request = true;

	if (do_poll) {
		auto io_thread = std::thread([&] {
			std::string s;
			while (keep_polling && std::getline(std::cin, s, '\n'))
			{
				printf("poll\n");
				new_request = true;
				std::size_t found = s.find("q");
				if (found != std::string::npos) {
					keep_polling = false;
					printf("Received quit message\n");
				}
				auto lock = std::unique_lock<std::mutex>(m);
				lock.unlock();
				cv.notify_all();
			}
			auto lock = std::unique_lock<std::mutex>(m);
			lock.unlock();
			cv.notify_all();
		});

		std::string nil_string = std::string();
		auto current_string = nil_string;

		/*-----------------------  Synchronize to Eyegaze --------------------------*/

		/*    This code keeps the GazeDemo loop synchronized with the real-time     */
		/*    Eyegaze image processing, i.e. looping at 60 Hz, but insures that all */
		/*    gazepoint data samples are processed, even if the GazeDemo loop gets  */
		/*    a little behind the real-time Eyegaze image processing.  Data         */
		/*    buffers allow the GazeDemo loop to process all past gazepoint data    */
		/*    samples even if the loop falls up to EG_BUFFER_LEN samples behind     */
		/*    real time.                                                            */
		while (keep_polling) {
			auto lock = std::unique_lock<std::mutex>(m);
			cv.wait(lock, [&] { return true; });
			current_string = new_string;
			lock.unlock();


			/*    If the ring buffer has overflowed,                                    */
			if (stEgControl.iNBufferOverflow > 0)
			{
				/*       The application program acts on data loss if necessary.            */
				//         (appropriate application code)
			}

			/*    The image processing software, running independently of this          */
			/*    application, produces a new eyegaze data sample every 16.67 milli-    */
			/*    seconds. If an unprocessed Eyegaze data sample is still available     */
			/*    for processing, EgGetData() returns immediately, allowing the         */
			/*    application to catch up with the Eyegaze image processing.  If the    */
			/*    next unprocessd sample has not yet arrived, EgGetData blocks until    */
			/*    data is available and then returns.  This call effectively puts the   */
			/*    application to sleep until new Eyegaze data is available to be        */
			/*    processed.                                                            */
			iVis = EgGetData(&stEgControl);

			/*-------------------- Process Next Eyegaze Data Sample --------------------*/

			/*    Compute the smoothed gazepoint, averaging over the last 12 samples.   */
			SmoothGazepoint(stEgControl.pstEgData->bGazeVectorFound,
				stEgControl.pstEgData->iIGaze,
				stEgControl.pstEgData->iJGaze,
				&iIGazeSmooth, &iJGazeSmooth, 12);

			/*    If the gazepoint was found this iteration,                            */
			if (stEgControl.pstEgData->bGazeVectorFound == TRUE)
			{
				SYSTEMTIME st;
				GetSystemTime(&st);
				double last_gaze_ts = st.wSecond + (((double)st.wMilliseconds) / 1000);
				//			double last_gaze_ts = difftime(time(0), 0);
				double last_gaze_x = iIGazeSmooth;// *65535 / iScreenWidth;
				double last_gaze_y = iJGazeSmooth;// *65535 / iScreenHeight;
				if (new_request) {
					printf("%i %i\n", iScreenWidth, iScreenHeight);
					printf("%f, %f, %f\n", last_gaze_x, last_gaze_y, last_gaze_ts);
					new_request = false;
				}
			}
		}
		printf("Done with loop, waiting on thread...\n");
		keep_polling = false;
		io_thread.join();
	}
	printf("Exiting\n");
	EgExit(&stEgControl);

	return 0;
}

