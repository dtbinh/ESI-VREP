//---------------------------------------------------------------------------------------
//		V-REP Filter Plugins
//			Author: Pablo Ram�n Soria.
//			Based on: V-REP code. Copyright 2006-2014 Dr. Marc Andreas Freese. All rights reserved. 
//			Date: 2014-02-10
//---------------------------------------------------------------------------------------

#include "v_repExtSimpleFilter.h"
#include "v_rep/include/v_repLib.h"
#include "pluginGlobals.h"

#include <iostream>
#include <sstream>
#include <vector>

#include <algorithms/segmentation/ColorClustering.h>
#include <core/types/ColorSpaceHSV8.h>

#ifdef _WIN32
	#include <direct.h>
#endif /* _WIN32 */

#if defined (__linux) || defined (__APPLE__)
	#include <string.h>
	#define _stricmp(x,y) strcasecmp(x,y)
#endif

// General information about the plugins implemented in the code
const int filterCount=2; // Number of filters coded in this pluging
int filterID[filterCount]={-1, -2}; // Filters with negative IDs won't have a dialog or special triggering conditions (negative IDs for simple filters!)
// Header ID (DEVELOPER_DATA_HEADER), filterIDs and parameter values of a filter are serialized!! (don't change at will!)
int nextFilterEnum=0; // used during enumeration
char* filterName[filterCount]={ "ESI-PabloRamonSoria:	Simple Red Segmentation",
								"ESI-PabloRamonSoria:	Color Cluster Segmentation"}; // Names of filters

// TODO 666: nombre de señales etc... cambiar o preconfigurar etc...
const std::string quad1Signal = "Quad1";


// Main functions of the V-REP plugins DLL
LIBRARY vrepLib;

VREP_DLLEXPORT unsigned char v_repStart(void* reservedPointer,int reservedInt)
{ // This is called just once, at the start of V-REP.

	// Dynamically load and bind V-REP functions:
	 // ******************************************
	 // 1. Figure out this plugin's directory:
	 char curDirAndFile[1024];
 #ifdef _WIN32
	 _getcwd(curDirAndFile, sizeof(curDirAndFile));
 #elif defined (__linux) || defined (__APPLE__)
	 getcwd(curDirAndFile, sizeof(curDirAndFile));
 #endif
	 std::string currentDirAndPath(curDirAndFile);
	 // 2. Append the V-REP library's name:
	 std::string temp(currentDirAndPath);
 #ifdef _WIN32
	 temp+="/v_rep.dll";
 #elif defined (__linux)
	 temp+="/libv_rep.so";
 #elif defined (__APPLE__)
	 temp+="/libv_rep.dylib";
 #endif /* __linux || __APPLE__ */
	 // 3. Load the V-REP library:
	 vrepLib=loadVrepLibrary(temp.c_str());
	 if (vrepLib==NULL)
	 {
		 std::cout << "Error, could not find or correctly load the V-REP library. Cannot start 'SimpleFilter' plugin./n";
		 return(0); // Means error, V-REP will unload this plugin
	 }
	 if (getVrepProcAddresses(vrepLib)==0)
	 {
		 std::cout << "Error, could not find all required functions in the V-REP library. Cannot start 'SimpleFilter' plugin./n";
		 unloadVrepLibrary(vrepLib);
		 return(0); // Means error, V-REP will unload this plugin
	 }
	 // ******************************************

	 // Check the version of V-REP:
	 // ******************************************
	 int vrepVer;
	 simGetIntegerParameter(sim_intparam_program_version,&vrepVer);
	 if (vrepVer<20604) // if V-REP version is smaller than 2.06.04
	 {
		 std::cout << "Sorry, your V-REP copy is somewhat old. Cannot start 'SimpleFilter' plugin.\n";
		 unloadVrepLibrary(vrepLib);
		 return(0); // Means error, V-REP will unload this plugin
	 }
	 // ******************************************


	return(5);	// initialization went fine, return the version number of this plugin!
				// version 1 was for V-REP 2.5.11 or earlier
				// version 2 was for V-REP 2.5.12 or earlier
				// version 3 was for V-REP versions before V-REP 2.6.7
				// version 4 is the Qt version
				// version 5 is since 10/1/2014 (new lock)
}

VREP_DLLEXPORT void v_repEnd()
{ // This is called just once, at the end of V-REP
	unloadVrepLibrary(vrepLib); // release the library
}

VREP_DLLEXPORT void* v_repMessage(int message,int* auxiliaryData,void* customData,int* replyData)
{ // This is called quite often. Just watch out for messages/events you want to handle

	// This function should not generate any error messages:
	int errorModeSaved;
	simGetIntegerParameter(sim_intparam_error_report_mode,&errorModeSaved);
	simSetIntegerParameter(sim_intparam_error_report_mode,sim_api_errormessage_ignore);

	void* retVal=NULL;	// Variable that store results of filters

	if (message==sim_message_eventcallback_imagefilter_enumreset)
		nextFilterEnum=0; // Reset the enumeration procedure for these filters

	if (message==sim_message_eventcallback_imagefilter_enumerate)
	{ // Filter enumeration procedure. Here we inform V-REP of all filters that this plugin has
		if (nextFilterEnum<filterCount)
		{ // This plugin has more filters to 'register' to V-REP!
			replyData[0]=DEVELOPER_DATA_HEADER;
			replyData[1]=filterID[nextFilterEnum];
			char* retValTmp=simCreateBuffer(int(strlen(filterName[nextFilterEnum])+1));
			strcpy(retValTmp,filterName[nextFilterEnum]);
			retVal=(void*)retValTmp;
			nextFilterEnum++;
		}
	}

	if (message==sim_message_eventcallback_imagefilter_process)
	{ // V-REP calls this when a plugin filter should be applied
		if (auxiliaryData[0]==DEVELOPER_DATA_HEADER)
		{ // yes the filter is in this plugin (the filter is identified by a header ID (in auxiliaryData[0]) and a filter ID (in auxiliaryData[1]))
			int res[2]={auxiliaryData[2],auxiliaryData[3]};		// Width and Height of the images/buffers/depth maps
			void** ptrs=(void**)customData;						// Special input

			// Pointers to images. http://www.coppeliarobotics.com/helpFiles/index.html - Entities/Scene Objects/Vision Sensors/Vision sensor filter composition
			float* inputImage=(float*)ptrs[0]; // original image from the vision sensor
			float* inputDepth=(float*)ptrs[1]; // original depth map from the vision sensor
			float* workImage=(float*)ptrs[2]; // work image --> Image to work on it
			float* buffer1=(float*)ptrs[3]; // buffer 1 image
			float* buffer2=(float*)ptrs[4]; // buffer 2 image
			float* outputImage=(float*)ptrs[5]; // output image
			unsigned char* params=(unsigned char*)ptrs[6];

			if(auxiliaryData[1] == -1){
				for (int i = 0 ; i < res[0]*res[1] ; i++)
				{
					if(		workImage[3*i + 0] > 0.75f &&
							workImage[3*i + 1] < 0.20f &&
							workImage[3*i + 2] < 0.20f){
						workImage[3*i + 0] = 1.0f;
						workImage[3*i + 1] = 0.0f;
						workImage[3*i + 2] = 0.0f;
					}
					else{
						workImage[3*i + 0] = 0.0f;
						workImage[3*i + 1] = 0.0f;
						workImage[3*i + 2] = 0.0f;
					}
				}
			} // SIMPLE RED SEGMENTATION

			//-------------------------------------------------------------------------
			else if (auxiliaryData[1]==-2) { // Filter: Color Cluster Segmentation
	
				// 666 TODO: displayear de alguna forma para luego hacer el EKF.
				std::vector<BOViL::ImageObject> objects;
				

				// 666 TODO: está recibiendo  todo vacío
				BOViL::algorithms::ColorClustering<float>(	workImage,
															res[0], 
															res[1], 
															10, 
															objects, 
															[](float *_a, float *_b, float *_c){	if(*_a > 0.705f && *_b < 0.32f && *_c < 0.32f){
																										*_a = 1.0f;
																										*_b = 0;
																										*_c = 0;
																										return 4;
																									} else{
																										*_a = 0;
																										*_b = 0;
																										*_c = 0;
																										return -1;
																									}
																									});

				simClearStringSignal(quad1Signal.c_str());

				for(unsigned int i = 0 ; i < objects.size() ; i ++){ /// 666 TODO: solo va  a funcionar con un objecto, revisar
					std::cout << "(x, y) == (" << objects[i].getCentroid().x << ", " << objects[i].getCentroid().y << ")" << std::endl;
					
					std::stringstream ss;
					ss << objects[i].getCentroid().x << "*" << objects[i].getCentroid().y;
					
					std::string msg = ss.str();
					simInt quadHandle = 0;
					quadHandle = simGetObjectHandle("Quadricopter");
					
					std::cout << "QuadHandle: " << quadHandle << "; message: " << msg << std::endl;

					//int result = simAddObjectCustomData(quadHandle, DEVELOPER_DATA_HEADER, msg.c_str(), msg.size());
					int result = simSetStringSignal(quad1Signal.c_str(), msg.c_str(), msg.size());

					if(result == -1)
						std::cout << "ERROR CUSTOM DATA" << std::endl;
					
					
					

				}
			}	// COLOR CLUSTER SEGMENTATION
			
			//-------------------------------------------------------------------------

			// We return auxiliary information that resulted from the image processing (that could be a vector, a direction, or other filter specific data)
			// That auxiliary information (as well as the trigger state) are retrieved with simHandleVisionSensor
			// For now we simply return two values: 42.0 and 99.0:
			//	-->replyData[1]=2; // the number of floats we return
			//	-->retVal=simCreateBuffer(sizeof(float)*2); // is automatically released by V-REP upon callback return
			//	-->((float*)retVal)[0]=42.0f;
			//	-->((float*)retVal)[1]=99.0f;
		}
	}


	// You can add more messages to handle here

	simSetIntegerParameter(sim_intparam_error_report_mode,errorModeSaved); // restore previous settings
	return(retVal);
}
