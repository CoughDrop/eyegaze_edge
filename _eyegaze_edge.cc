#include <node.h>
#include <time.h>
#include "include\EgWin.h"

#pragma comment (lib, "lctigaze.lib")

namespace eyegaze_edge {
	using v8::Local;
	using v8::Persistent;
	using v8::Handle;
	using v8::Isolate;
	using v8::FunctionCallbackInfo;
	using v8::Object;
	using v8::HandleScope;
	using v8::String;
	using v8::Number;
	using v8::Value;
	using v8::Null;
	using v8::Function;
	using node::AtExit;

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

	void jsHello(const FunctionCallbackInfo<Value>& args) {
		Isolate* isolate = args.GetIsolate();
		args.GetReturnValue().Set(String::NewFromUtf8(isolate, "world"));
	}

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

	static double last_gaze_x;
	static double last_gaze_y;
	static double last_gaze_ts;

	void jsPing(const FunctionCallbackInfo<Value>& args) {
		Isolate* isolate = args.GetIsolate();
		Local<Object> obj = Object::New(isolate);
		Local<String> str = String::NewFromUtf8(isolate, "x");

		obj->Set(String::NewFromUtf8(isolate, "gaze_x"), Number::New(isolate, last_gaze_x));
		obj->Set(String::NewFromUtf8(isolate, "gaze_y"), Number::New(isolate, last_gaze_y));
		obj->Set(String::NewFromUtf8(isolate, "gaze_ts"), Number::New(isolate, last_gaze_ts));

		args.GetReturnValue().Set(obj);
	}

	bool setup() {
		bool success = true;
			/* This function is the core of the Eyegaze application that is executed    */
			/* as a separate thread when the program starts.                            */

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
			if (rc != 0)
			{
				printf("Error %i Initializing Eyegaze Edge", rc);
				success = false;
			}
			else {
				printf("Eyegaze Edge initialized!");
				EgCalibrate2(&stEgControl, EG_CALIBRATE_DISABILITY_APP);
			}


		return success;
	}

	bool teardown() {
		bool success = true;
		EgExit(&stEgControl);
		return success;
	}

	void jsSetup(const FunctionCallbackInfo<Value>& args) {
		bool result = setup();
		Isolate* isolate = args.GetIsolate();
		args.GetReturnValue().Set(result);
	}

	void jsTeardown(const FunctionCallbackInfo<Value>& args) {
		bool result = teardown();
		Isolate* isolate = args.GetIsolate();
		args.GetReturnValue().Set(result);
	}

	void jsListen(const FunctionCallbackInfo<Value>& args) {
		bool result = true;
		/* Tell Eyegaze to start eyetracking, i.e. to begin image processing.       */
		/* Note: No eyetracking takes place until this flag is set to true, and     */
		/* eye image processing stops when this flag is reset to false.             */
		stEgControl.bTrackingActive = TRUE;

		/*-----------------------  Synchronize to Eyegaze --------------------------*/

		/*    This code keeps the GazeDemo loop synchronized with the real-time     */
		/*    Eyegaze image processing, i.e. looping at 60 Hz, but insures that all */
		/*    gazepoint data samples are processed, even if the GazeDemo loop gets  */
		/*    a little behind the real-time Eyegaze image processing.  Data         */
		/*    buffers allow the GazeDemo loop to process all past gazepoint data    */
		/*    samples even if the loop falls up to EG_BUFFER_LEN samples behind     */
		/*    real time.                                                            */
		while(stEgControl.bTrackingActive == TRUE) {
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
				last_gaze_ts = difftime(time(0), 0);
				last_gaze_x = iIGazeSmooth * 65535 / iScreenWidth;
				last_gaze_y = iJGazeSmooth * 65535 / iScreenHeight;
			}
		}

		Isolate* isolate = args.GetIsolate();
		args.GetReturnValue().Set(result);
	}

	void jsStopListening(const FunctionCallbackInfo<Value>& args) {
		bool result = true;
		/* Tell Eyegaze to start eyetracking, i.e. to begin image processing.       */
		/* Note: No eyetracking takes place until this flag is set to true, and     */
		/* eye image processing stops when this flag is reset to false.             */
		stEgControl.bTrackingActive = FALSE;
		Isolate* isolate = args.GetIsolate();
		args.GetReturnValue().Set(result);
	}

	void init(Local<Object> exports) {
		NODE_SET_METHOD(exports, "hello", jsHello);
		NODE_SET_METHOD(exports, "listen", jsListen);
		NODE_SET_METHOD(exports, "stop_listening", jsStopListening);
		NODE_SET_METHOD(exports, "setup", jsSetup);
		NODE_SET_METHOD(exports, "teardown", jsTeardown);
		NODE_SET_METHOD(exports, "ping", jsPing);
	}

	NODE_MODULE(eyegaze_edge, init)
}