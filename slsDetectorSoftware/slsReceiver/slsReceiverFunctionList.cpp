#ifdef SLS_RECEIVER_FUNCTION_LIST
/********************************************//**
 * @file slsReceiverFunctionList.cpp
 * @short does all the functions for a receiver, set/get parameters, start/stop etc.
 ***********************************************/


#include "slsReceiverFunctionList.h"

#include "moench02ModuleData.h"
#include "gotthardModuleData.h"
#include "gotthardShortModuleData.h"


#include <signal.h>  		// SIGINT
#include <sys/stat.h> 		// stat
#include <sys/socket.h>		// socket(), bind(), listen(), accept(), shut down
#include <arpa/inet.h>		// sock_addr_in, htonl, INADDR_ANY
#include <stdlib.h>			// exit()
#include <iomanip>			//set precision
#include <sys/mman.h>		//munmap



#include <string.h>
#include <iostream>
using namespace std;




slsReceiverFunctionList::slsReceiverFunctionList(detectorType det):
#ifdef EIGER_RECEIVER_H
					receiver(NULL),
#endif
					myDetectorType(det),
					status(IDLE),
					udpSocket(NULL),
					server_port(DEFAULT_UDP_PORTNO),
					eth(NULL),
					maxPacketsPerFile(0),
					enableFileWrite(1),
					fileIndex(0),
					frameIndexNeeded(0),
					acqStarted(false),
					measurementStarted(false),
					startFrameIndex(0),
					frameIndex(0),
					packetsCaught(0),
					totalPacketsCaught(0),
					packetsInFile(0),
					startAcquisitionIndex(0),
					acquisitionIndex(0),
					packetsPerFrame(GOTTHARD_PACKETS_PER_FRAME),
					frameIndexMask(GOTTHARD_FRAME_INDEX_MASK),
					packetIndexMask(GOTTHARD_PACKET_INDEX_MASK),
					frameIndexOffset(GOTTHARD_FRAME_INDEX_OFFSET),
					acquisitionPeriod(SAMPLE_TIME_IN_NS),
					shortFrame(-1),
					currframenum(0),
					prevframenum(0),
					bufferSize(GOTTHARD_BUFFER_SIZE),
					latestData(NULL),
					guiDataReady(0),
					guiData(NULL),
					guiFileName(NULL),
					nFrameToGui(0),
					fifosize(GOTTHARD_FIFO_SIZE),
					numJobsPerThread(-1),
					mem0(NULL),
					dataCompression(false),
					fifo(NULL),
					fifoFree(NULL),
					buffer(NULL),
					numWriterThreads(1),
					thread_started(0),
					currentWriterThreadIndex(-1),
					totalListeningFrameCount(0),
					sfilefd(NULL),
					writerthreads_mask(0x0),
					listening_thread_running(0),
					killListeningThread(0),
					killAllWritingThreads(0),
					cbAction(DO_EVERYTHING),
					startAcquisitionCallBack(NULL),
					pStartAcquisition(NULL),
					acquisitionFinishedCallBack(NULL),
					pAcquisitionFinished(NULL),
					rawDataReadyCallBack(NULL),
					pRawDataReady(NULL){

	maxPacketsPerFile = MAX_FRAMES_PER_FILE * packetsPerFrame;
	//moench variables
	if(myDetectorType == MOENCH){
		fifosize = MOENCH_FIFO_SIZE;
		bufferSize = MOENCH_BUFFER_SIZE;
		packetsPerFrame = MOENCH_PACKETS_PER_FRAME;
		maxPacketsPerFile = MOENCH_MAX_FRAMES_PER_FILE * MOENCH_PACKETS_PER_FRAME;
		frameIndexMask = MOENCH_FRAME_INDEX_MASK;
		frameIndexOffset = MOENCH_FRAME_INDEX_OFFSET;
		packetIndexMask = MOENCH_PACKET_INDEX_MASK;
	}
	else if(myDetectorType == EIGER){
#ifdef EIGER_RECEIVER_H
		receiver = new EigerReceiver::create();
#endif
	}

	//variable initialization
	onePacketSize = bufferSize/packetsPerFrame;
	eth = new char[MAX_STR_LENGTH];
	guiFileName = new char[MAX_STR_LENGTH];
	latestData = new char[bufferSize];
	strcpy(eth,"");
	strcpy(guiFileName,"");
	strcpy(latestData,"");
	strcpy(savefilename,"");
	strcpy(filePath,"");
	strcpy(fileName,"run");

	cmSub = NULL;
	for(int i=0;i<numWriterThreads;i++){
		commonModeSubtractionEnable = false;
		singlePhotonDet[i] = NULL;
		receiverdata[i] = NULL;
#ifdef MYROOT1
		myTree[i] = (NULL);
		myFile[i] = (NULL);
#endif
	}


	setupFifoStructure();

	pthread_mutex_init(&dataReadyMutex,NULL);
	pthread_mutex_init(&status_mutex,NULL);
	pthread_mutex_init(&progress_mutex,NULL);
	pthread_mutex_init(&write_mutex,NULL);



	//to increase socket receiver buffer size and max length of input queue by changing kernel settings
	if(system("echo $((100*1024*1024)) > /proc/sys/net/core/rmem_max"))
		cout << "\nWARNING: Could not change socket receiver buffer size in file /proc/sys/net/core/rmem_max" << endl;
	else if(system("echo 250000 > /proc/sys/net/core/netdev_max_backlog"))
		cout << "\nWARNING: Could not change max length of input queue in file /proc/sys/net/core/netdev_max_backlog" << endl;
	/** permanent setting heiner
	net.core.rmem_max = 104857600 # 100MiB
	net.core.netdev_max_backlog = 250000
	sysctl -p
	// from the manual
	sysctl -w net.core.rmem_max=16777216
	sysctl -w net.core.netdev_max_backlog=250000
	 */


	//threads
	pthread_mutex_lock(&status_mutex);
	status = IDLE;
	pthread_mutex_unlock(&(status_mutex));

	if(createListeningThreads() == FAIL){
		cout << "ERROR: Could not create listening thread" << endl;
		exit (-1);
	}

	if(createWriterThreads() == FAIL){
		cout << "ERROR: Could not create writer threads" << endl;
		exit (-1);
	}

	setThreadPriorities();
}



slsReceiverFunctionList::~slsReceiverFunctionList(){
	createListeningThreads(true);
	createWriterThreads(true);
	for(int i=0;i<numWriterThreads;i++){
		if(singlePhotonDet[i])
			delete singlePhotonDet[i];
		if(receiverdata[i])
			delete receiverdata[i];
	}
	if(udpSocket) 		delete udpSocket;
	if(eth) 			delete [] eth;
	if(latestData) 		delete [] latestData;
	if(guiFileName) 	delete [] guiFileName;
	if(mem0)			free(mem0);
	if(fifo)			delete fifo;
}



void slsReceiverFunctionList::setEthernetInterface(char* c){
	strcpy(eth,c);
}



uint32_t slsReceiverFunctionList::getFrameIndex(){
	if(!packetsCaught)
		frameIndex=0;
	else
		frameIndex = currframenum - startFrameIndex;
	return frameIndex;
}



uint32_t slsReceiverFunctionList::getAcquisitionIndex(){
	if(!totalPacketsCaught)
		acquisitionIndex=0;
	else
		acquisitionIndex = currframenum - startAcquisitionIndex;
	return acquisitionIndex;
}



#ifdef EIGER_RECEIVER_H

int32_t slsReceiverFunctionList::setNumberOfFrames(int32_t fnum){
	if(fnum >= 0)
		receiver->setNumberOfFrames(fnum);
	return receiver->getNumberOfFrames();
}

int32_t slsReceiverFunctionList::setScanTag(int32_t stag){
	if(stag >= 0)
		receiver->setScanTag(stag);
	return receiver->getScanTag();
}

int32_t slsReceiverFunctionList::setDynamicRange(int32_t dr){
	if(dr >= 0)
		receiver->setDynamicRange(dr);
	return receiver->getDynamicRange();
}


char* slsReceiverFunctionList::setDetectorHostname(char c[]){
	if((strlen(c))&& (receiver->getDetectorHostname() == NULL))
		receiver->initialize(c);
	}
	return  receiver->getDetectorHostname();
}


#endif



char* slsReceiverFunctionList::setFileName(char c[]){
	if(strlen(c)){
#ifdef EIGER_RECEIVER_H
		receiver->setFileName(c);
#else
		strcpy(fileName,c);
#endif
	}
#ifdef EIGER_RECEIVER_H
		return  receiver->getFileName();
#else
	return getFileName();
#endif
}



char* slsReceiverFunctionList::setFilePath(char c[]){
	if(strlen(c)){
		//check if filepath exists
		struct stat st;
		if(stat(c,&st) == 0){
#ifdef EIGER_RECEIVER_H
			receiver->setFileName(c);
#else
			strcpy(filePath,c);
#endif
		}else{
			strcpy(filePath,"");
			cout<<"FilePath does not exist:"<<filePath<<endl;
		}
	}
#ifdef EIGER_RECEIVER_H
		return  receiver->getFilePath();
#else
	return getFilePath();
#endif
}



int slsReceiverFunctionList::setFileIndex(int i){
	if(i>=0)
		fileIndex = i;
	return getFileIndex();
}



int slsReceiverFunctionList::setEnableFileWrite(int i){
	if(i!=-1){
#ifdef EIGER_RECEIVER_H
		receiver->setEnableFileWrite(i);
#else
		enableFileWrite=i;
#endif
	}
#ifdef EIGER_RECEIVER_H
		return  receiver->getEnableFileWrite();
#else
	return enableFileWrite;
#endif
}




void slsReceiverFunctionList::resetTotalFramesCaught(){
	acqStarted = false;
	startAcquisitionIndex = 0;
	totalPacketsCaught = 0;
}



int slsReceiverFunctionList::setShortFrame(int i){
	shortFrame=i;

	if(shortFrame!=-1){
		bufferSize = GOTTHARD_SHORT_BUFFER_SIZE;
		maxPacketsPerFile = SHORT_MAX_FRAMES_PER_FILE * GOTTHARD_SHORT_PACKETS_PER_FRAME;
		packetsPerFrame = GOTTHARD_SHORT_PACKETS_PER_FRAME;
		frameIndexMask = GOTTHARD_SHORT_FRAME_INDEX_MASK;
		frameIndexOffset = GOTTHARD_SHORT_FRAME_INDEX_OFFSET;

	}else{
		bufferSize = GOTTHARD_BUFFER_SIZE;
		maxPacketsPerFile = MAX_FRAMES_PER_FILE * GOTTHARD_PACKETS_PER_FRAME;
		packetsPerFrame = GOTTHARD_PACKETS_PER_FRAME;
		frameIndexMask = GOTTHARD_FRAME_INDEX_MASK;
		frameIndexOffset = GOTTHARD_FRAME_INDEX_OFFSET;
	}


	onePacketSize = bufferSize/packetsPerFrame;

	deleteFilter();
	if(dataCompression)
		setupFilter();

	return shortFrame;
}






int slsReceiverFunctionList::setNFrameToGui(int i){
	if(i>=0){
		nFrameToGui = i;
		setupFifoStructure();
	}
	return nFrameToGui;
}




int64_t slsReceiverFunctionList::setAcquisitionPeriod(int64_t index){
	if(index >= 0){
		if(index != acquisitionPeriod){
			acquisitionPeriod = index;
			setupFifoStructure();
		}
	}
	return acquisitionPeriod;
}



int slsReceiverFunctionList::enableDataCompression(bool enable){
	cout << "Data compression ";
	if(enable)
		cout << "enabled" << endl;
	else
		cout << "disabled" << endl;
	//delete filter for the current number of threads
	deleteFilter();

	dataCompression = enable;
	pthread_mutex_lock(&status_mutex);
	writerthreads_mask = 0x0;
	pthread_mutex_unlock(&(status_mutex));

	createWriterThreads(true);

	if(enable)
		numWriterThreads = MAX_NUM_WRITER_THREADS;
	else
		numWriterThreads = 1;

	if(createWriterThreads() == FAIL){
		cout << "ERROR: Could not create writer threads" << endl;
		return FAIL;
	}
	setThreadPriorities();


	if(enable)
		setupFilter();

	return OK;
}




void slsReceiverFunctionList::deleteFilter(){
	int i;
	cmSub=NULL;

	for(i=0;i<numWriterThreads;i++){
		if(singlePhotonDet[i]){
			delete singlePhotonDet[i];
			singlePhotonDet[i] = NULL;
		}
		if(receiverdata[i]){
			delete receiverdata[i];
			receiverdata[i] = NULL;
		}
	}
}



void slsReceiverFunctionList::setupFilter(){
	double hc = 0;
	double sigma = 5;
	int sign = 1;
	int csize;
	int i;

	if (commonModeSubtractionEnable)
		cmSub=new moenchCommonMode();

	switch(myDetectorType){
	case MOENCH:
		csize = 3;
		for(i=0;i<numWriterThreads;i++)
			receiverdata[i]=new moench02ModuleData(hc);
		break;
	default:
		csize = 1;
		if(shortFrame == -1){
			for(i=0;i<numWriterThreads;i++)
				receiverdata[i]=new gotthardModuleData(hc);
		}else{
			for(i=0;i<numWriterThreads;i++)
				receiverdata[i]=new gotthardShortModuleData(hc);
		}
		break;
	}

	for(i=0;i<numWriterThreads;i++)
		singlePhotonDet[i]=new singlePhotonDetector<uint16_t>(receiverdata[i], csize, sigma, sign, cmSub);

}


void slsReceiverFunctionList::readFrame(char* c,char** raw){
	//point to gui data
	if (guiData == NULL)
		guiData = latestData;

	//copy data and filename
	strcpy(c,guiFileName);

	//could not get gui data
	if(!guiDataReady){
		*raw = NULL;
	}
	//data ready, set guidata to receive new data
	else{
		*raw = guiData;
		guiData = NULL;

		pthread_mutex_lock(&dataReadyMutex);
		guiDataReady = 0;
		pthread_mutex_unlock(&dataReadyMutex);
		if((nFrameToGui) && (writerthreads_mask)){
			//release after getting data
			sem_post(&smp);
		}
	}
}





void slsReceiverFunctionList::copyFrameToGui(char* startbuf){

	//random read when gui not ready
	if((!nFrameToGui) && (!guiData)){
		pthread_mutex_lock(&dataReadyMutex);
		guiDataReady=0;
		pthread_mutex_unlock(&dataReadyMutex);
	}

	//random read or nth frame read, gui needs data now
	else{
		//nth frame read, block current process if the guireader hasnt read it yet
		if(nFrameToGui)
			sem_wait(&smp);

		pthread_mutex_lock(&dataReadyMutex);
		guiDataReady=0;
		//send the first one
		memcpy(latestData,startbuf,bufferSize);
		strcpy(guiFileName,savefilename);
		guiDataReady=1;
		pthread_mutex_unlock(&dataReadyMutex);
	}
}




void slsReceiverFunctionList::setupFifoStructure(){
	int64_t i;
	int oldn = numJobsPerThread;

	//if every nth frame mode
	if(nFrameToGui)
		numJobsPerThread = nFrameToGui;

	//random nth frame mode
	else{
		if(!acquisitionPeriod)
			i = SAMPLE_TIME_IN_NS;
		else
			i = SAMPLE_TIME_IN_NS/acquisitionPeriod;
		if (i > MAX_JOBS_PER_THREAD)
			numJobsPerThread = MAX_JOBS_PER_THREAD;
		else if (i < 1)
			numJobsPerThread = 1;
		else
			numJobsPerThread = i;
	}

	//if same, return
	if(oldn == numJobsPerThread)
		return;


	//otherwise memory too much if numjobsperthread is at max = 1000
	fifosize = GOTTHARD_FIFO_SIZE;
	if(myDetectorType == MOENCH)
		fifosize = MOENCH_FIFO_SIZE;

	if(fifosize % numJobsPerThread)
		fifosize = (fifosize/numJobsPerThread)+1;
	else
		fifosize = fifosize/numJobsPerThread;


	cout << "Number of Frames per buffer:" << numJobsPerThread << endl;
	cout << "Fifo Size:" << fifosize << endl;

	/*
	//for testing
	 numJobsPerThread = 3; fifosize = 11;
	 */

	//deleting old structure and creating fifo structure
	if(fifoFree){
		while(!fifoFree->isEmpty())
			fifoFree->pop(buffer);
		delete fifoFree;
	}
	if(fifo)	delete fifo;
	if(mem0) 	free(mem0);
	fifoFree 	= new CircularFifo<char>(fifosize);
	fifo 		= new CircularFifo<char>(fifosize);


	//allocate memory
	mem0=(char*)malloc((bufferSize * numJobsPerThread + HEADER_SIZE_NUM_TOT_PACKETS)*fifosize);
	/** shud let the client know about this */
	if (mem0==NULL){
		cout<<"++++++++++++++++++++++ COULD NOT ALLOCATE MEMORY FOR LISTENING !!!!!!!+++++++++++++++++++++" << endl;
		exit(-1);
	}
	buffer=mem0;
	//push the addresses into freed fifoFree and writingFifoFree
	while (buffer<(mem0+(bufferSize * numJobsPerThread + HEADER_SIZE_NUM_TOT_PACKETS)*(fifosize-1))) {
		fifoFree->push(buffer);
		buffer+=(bufferSize * numJobsPerThread + HEADER_SIZE_NUM_TOT_PACKETS);
	}

	cout << "Fifo structure reconstructed" << endl;
}






int slsReceiverFunctionList::createUDPSocket(){
	if(udpSocket)
		udpSocket->ShutDownSocket();

	//if eth is mistaken with ip address
	if (strchr(eth,'.')!=NULL)
		strcpy(eth,"");

	//if no eth, listen to all
	if(!strlen(eth)){
		cout<<"warning:eth is empty.listening to all"<<endl;

		udpSocket = new genericSocket(server_port,genericSocket::UDP,bufferSize,packetsPerFrame);
	}
	//normal socket
	else{
		cout<<"eth:"<<eth<<endl;
		udpSocket = new genericSocket(server_port,genericSocket::UDP,bufferSize,packetsPerFrame,eth);
	}

	//error
	int iret = udpSocket->getErrorStatus();
	if (iret){
//#ifdef VERBOSE
		cout << "Could not create UDP socket on port " << server_port  << " error:" << iret << endl;
//#endif

		return FAIL;
	}
	return OK;
}




int slsReceiverFunctionList::createListeningThreads(bool destroy){
	void* status;

	killListeningThread = 0;

	pthread_mutex_lock(&status_mutex);
	listening_thread_running = 0;
	pthread_mutex_unlock(&(status_mutex));

	if(!destroy){
		//listening thread
		cout << "Creating Listening Thread" << endl;
		sem_init(&listensmp,1,0);
		if(pthread_create(&listening_thread, NULL,startListeningThread, (void*) this)){
			cout << "Could not create listening thread" << endl;
			return FAIL;
		}
#ifdef VERBOSE
		cout << "Listening thread created successfully." << endl;
#endif
	}else{
		cout<<"Destroying Listening Thread"<<endl;
		killListeningThread = 1;
		sem_post(&listensmp);
		pthread_join(listening_thread, &status);
		killListeningThread = 0;
		cout << "Listening thread destroyed" << endl;

	}

	return OK;

}






int slsReceiverFunctionList::createWriterThreads(bool destroy){
	int i;
	void* status;

	killAllWritingThreads = 0;

	pthread_mutex_lock(&status_mutex);
	writerthreads_mask = 0x0;
	createfile_mask = 0x0;
	pthread_mutex_unlock(&(status_mutex));


	if(!destroy){

		//start writer threads
		cout << "Creating Writer Threads";

		currentWriterThreadIndex = -1;

		for(i = 0; i < numWriterThreads; ++i){
			sem_init(&writersmp[i],1,0);
			thread_started = 0;
			currentWriterThreadIndex = i;
			if(pthread_create(&writing_thread[i], NULL,startWritingThread, (void*) this)){
				cout << "Could not create writer thread with index " << i << endl;
				return FAIL;
			}
			while(!thread_started);
			cout << ".";
			cout << flush;
		}
#ifdef VERBOSE
		cout << endl << "Writer threads created successfully." << endl;
#else
		cout << endl;
#endif

	}else{
		cout << "Destroying Writer Thread" << endl;
		killAllWritingThreads = 1;
		for(i = 0; i < numWriterThreads; ++i){
			sem_post(&writersmp[i]);
			pthread_join(writing_thread[i],&status);
			cout <<"."<<flush;
		}
		killAllWritingThreads = 0;
		cout << endl << "Writer threads destroyed" << endl;
	}

	return OK;
}









void slsReceiverFunctionList::setThreadPriorities(){

	//assign priorities
	struct sched_param tcp_param, listen_param, write_param;
	int policy= SCHED_RR;
	bool rights = true;

	tcp_param.sched_priority = 50;
	listen_param.sched_priority = 99;
	write_param.sched_priority = 90;

	if (pthread_setschedparam(listening_thread, policy, &listen_param) == EPERM)
		rights = false;
	for(int i = 0; i < numWriterThreads; ++i)
		if(rights)
			if (pthread_setschedparam(writing_thread[i], policy, &write_param) == EPERM){
				rights = false;
				break;
			}
	if (pthread_setschedparam(pthread_self(),5 , &tcp_param) == EPERM)
		rights = false;

	if(!rights)
		cout << "WARNING: Could not prioritize threads. You need to be super user for that." << endl;

}






int slsReceiverFunctionList::setupWriter(){

	//reset writing thread variables
	packetsInFile=0;
	packetsCaught=0;
	frameIndex=0;
	if(sfilefd) sfilefd=NULL;
	guiData = NULL;
	guiDataReady=0;
	strcpy(guiFileName,"");
	cbAction = DO_EVERYTHING;

	pthread_mutex_lock(&status_mutex);
	writerthreads_mask = 0x0;
	createfile_mask = 0x0;
	ret_createfile = OK;
	pthread_mutex_unlock(&status_mutex);

	//printouts
	cout << "Max Packets Per File:" << maxPacketsPerFile << endl;
	if (rawDataReadyCallBack)
		cout << "Note: Data Write has been defined exernally" << endl;
	if (dataCompression)
		cout << "Data Compression is enabled with " << numJobsPerThread << " number of jobs per thread" << endl;
	if(nFrameToGui)
		cout << "Sending every " << nFrameToGui << "th frame to gui" <<  endl;



	//acquisition start call back returns enable write
	if (startAcquisitionCallBack)
		cbAction=startAcquisitionCallBack(filePath,fileName,fileIndex,bufferSize,pStartAcquisition);

	if(cbAction < DO_EVERYTHING)
		cout << endl << "Note: Call back activated. Data saving must be taken care of by user in call back." << endl;
	else if(enableFileWrite==0)
		cout << endl << "Note: Data will not be saved" << endl;



	//creating first file

	//mask
	pthread_mutex_lock(&status_mutex);
	for(int i=0;i<numWriterThreads;i++)
		createfile_mask|=(1<<i);
	pthread_mutex_unlock(&status_mutex);

	for(int i=0;i<numWriterThreads;i++){
#ifdef VERYDEBUG
		cout << i << " gonna post 1st sem" << endl;
#endif
		sem_post(&writersmp[i]);
	}
	//wait till its created
	while(createfile_mask){
		//cout<<"*"<<flush;
		usleep(5000);
	}
	if (createfile_mask)
		cout <<"*********************************************sooo weird:"<<createfile_mask<<endl;


	if(dataCompression){
#if (defined(MYROOT1) && defined(ALLFILE_DEBUG)) || !defined(MYROOT1)
		if(ret_createfile != FAIL){
			int ret = createNewFile();
			if(ret == FAIL)
				ret_createfile = FAIL;
		}
#endif
	}

	return ret_createfile;

}






int slsReceiverFunctionList::createCompressionFile(int ithr, int iframe){
#ifdef MYROOT1
	char temp[MAX_STR_LENGTH];
		//create file name for gui purposes, and set up acquistion parameters
		sprintf(temp, "%s/%s_fxxx_%d_%d.root", filePath,fileName,fileIndex,ithr);
		//file
		myFile[ithr] = new TFile(temp,"RECREATE");/** later  return error if it exists */
		cout << "Thread " << ithr << ": created File: "<< temp << endl;
		//tree
		sprintf(temp, "%s_fxxx_%d_%d",fileName,fileIndex,ithr);
		myTree[ithr]=singlePhotonDet[ithr]->initEventTree(temp, &iframe);
		//resets the pedestalSubtraction array and the commonModeSubtraction
		singlePhotonDet[ithr]->newDataSet();
		if(myFile[ithr]==NULL){
			cout<<"file null"<<endl;
			return FAIL;
		}
		if(!myFile[ithr]->IsOpen()){
			cout<<"file not open"<<endl;
			return FAIL;
		}
#endif
	return OK;
}




int slsReceiverFunctionList::createNewFile(){

	//create file name
	if(frameIndexNeeded==-1)
		sprintf(savefilename, "%s/%s_%d.raw", filePath,fileName,fileIndex);
	else
		sprintf(savefilename, "%s/%s_f%012d_%d.raw", filePath,fileName,(packetsCaught/packetsPerFrame),fileIndex);

	//if filewrite and we are allowed to write
	if(enableFileWrite && cbAction > DO_NOTHING){
		//close
		if(sfilefd){
			fclose(sfilefd);
			sfilefd = NULL;
		}
		//open file
		if (NULL == (sfilefd = fopen((const char *) (savefilename), "w"))){
			cout << "Error: Could not create file " << savefilename << endl;
			return FAIL;
		}
		//setting buffer
		setvbuf(sfilefd,NULL,_IOFBF,BUF_SIZE);

		//printing packet losses and file names
		if(!packetsCaught)
			cout << savefilename << endl;
		else{
			cout << savefilename
					<< "\tpacket loss "
					<< setw(4)<<fixed << setprecision(4)<< dec <<
					(int)((((currframenum-prevframenum)-(packetsInFile/packetsPerFrame))/(double)(currframenum-prevframenum))*100.000)
					<< "%\tframenum "
					<< dec << currframenum //<< "\t\t p " << prevframenum
					<< "\tindex " << dec << getFrameIndex()
					<< "\tlost " << dec << (((int)(currframenum-prevframenum))-(packetsInFile/packetsPerFrame)) << endl;

		}
	}

	//reset counters for each new file
	if(packetsCaught){
		prevframenum = currframenum;
		packetsInFile = 0;
	}

	return OK;
}








void slsReceiverFunctionList::closeFile(int ithr){
#ifdef VERBOSE
	cout << "In closeFile for thread " << ithr << endl;
#endif

	if(!dataCompression){
		if(sfilefd){
#ifdef VERBOSE
			cout << "sfield:" << (int)sfilefd << endl;
#endif
			fclose(sfilefd);
			sfilefd = NULL;
		}
	}
	//compression
	else{
#if (defined(MYROOT1) && defined(ALLFILE_DEBUG)) || !defined(MYROOT1)
		if(sfilefd){
#ifdef VERBOSE
			cout << "sfield:" << (int)sfilefd << endl;
#endif
			fclose(sfilefd);
			sfilefd = NULL;
		}
#endif

#ifdef MYROOT1
		pthread_mutex_lock(&write_mutex);
		//write to file
		if(myTree[ithr] && myFile[ithr]){
			myFile[ithr] = myTree[ithr]->GetCurrentFile();

			if(myFile[ithr]->Write())
				//->Write(tall->GetName(),TObject::kOverwrite);
				cout << "Thread " << ithr <<": wrote frames to file" << endl;
			else
				cout << "Thread " << ithr << ": could not write frames to file" << endl;

		}else
			cout << "Thread " << ithr << ": could not write frames to file: No file or No Tree" << endl;
		//close file
		if(myTree[ithr] && myFile[ithr])
			myFile[ithr] = myTree[ithr]->GetCurrentFile();
		if(myFile[ithr] != NULL)
			myFile[ithr]->Close();
		myFile[ithr] = NULL;
		myTree[ithr] = NULL;
		pthread_mutex_unlock(&write_mutex);

#endif
	}
}





int slsReceiverFunctionList::startReceiver(char message[]){
//#ifdef VERBOSE
	cout << "Starting Receiver" << endl;
//#endif


	//reset listening thread variables
	measurementStarted = false;
	startFrameIndex = 0;
	totalListeningFrameCount = 0;

	//udp socket
	if(createUDPSocket() == FAIL){
		strcpy(message,"Could not create UDP Socket.\n");
		cout << endl << message << endl;
		return FAIL;
	}
	cout << "UDP socket created successfully on port " << server_port << endl;


	if(setupWriter() == FAIL){
		//stop udp socket
		if(udpSocket)
			udpSocket->ShutDownSocket();

		sprintf(message,"Could not create file %s.\n",savefilename);
		return FAIL;
	}
	cout << "Successfully created file(s)" << endl;

	//done to give the gui some proper name instead of always the last file name
	if(dataCompression)
		sprintf(savefilename, "%s/%s_fxxx_%d_xx.root", filePath,fileName,fileIndex);

	//initialize semaphore
	sem_init(&smp,1,0);

	//status
	pthread_mutex_lock(&status_mutex);
	status = RUNNING;
	for(int i=0;i<numWriterThreads;i++)
		writerthreads_mask|=(1<<i);
	listening_thread_running = 1;
	pthread_mutex_unlock(&(status_mutex));


	//start listening /writing
	sem_post(&listensmp);
	for(int i=0; i < numWriterThreads; ++i){
		sem_post(&writersmp[i]);
	}

	cout << "Receiver Started.\nStatus:" << status << endl;

	return OK;
}




int slsReceiverFunctionList::stopReceiver(){
//#ifdef VERBOSE
	cout << "Stopping Receiver" << endl;
//#endif

	if(status == RUNNING)
		startReadout();

	while(status == TRANSMITTING)
		usleep(5000);

	//semaphore destroy
	sem_post(&smp);
	sem_destroy(&smp);

	//change status
	pthread_mutex_lock(&status_mutex);
	status = IDLE;
	pthread_mutex_unlock(&(status_mutex));

	cout << "Receiver Stopped.\nStatus:" << status << endl;
	return OK;
}





void slsReceiverFunctionList::startReadout(){
	//wait so that all packets which take time has arrived
	usleep(50000);

	pthread_mutex_lock(&status_mutex);
	status = TRANSMITTING;
	pthread_mutex_unlock(&status_mutex);
	cout << "Status: Transmitting" << endl;

	//kill udp socket to tell the listening thread to push last packet
	if(udpSocket)
		udpSocket->ShutDownSocket();

}



void* slsReceiverFunctionList::startListeningThread(void* this_pointer){
	((slsReceiverFunctionList*)this_pointer)->startListening();

	return this_pointer;
}



void* slsReceiverFunctionList::startWritingThread(void* this_pointer){
	((slsReceiverFunctionList*)this_pointer)->startWriting();
	return this_pointer;
}






int slsReceiverFunctionList::startListening(){
#ifdef VERYVERBOSE
	cout << "In startListening()" << endl;
#endif

	int lastpacketoffset, expected, rc, packetcount, maxBufferSize, carryonBufferSize;
	int lastframeheader;// for moench to check for all the packets in last frame
	char* tempchar = NULL;


	while(1){
		//variables that need to be checked/set before each acquisition
		carryonBufferSize = 0;
		maxBufferSize = packetsPerFrame * numJobsPerThread * onePacketSize;
		if(tempchar) {delete [] tempchar;tempchar = NULL;}
		tempchar = new char[onePacketSize * (packetsPerFrame - 1)]; //gotthard: 1packet size, moench:39 packet size


		while(listening_thread_running){

			//pop
			fifoFree->pop(buffer);
#ifdef VERYDEBUG
			cout << "*** popped from fifo free" << (void*)buffer << endl;
#endif

			//receive
			if(!carryonBufferSize){
				rc = udpSocket->ReceiveDataOnly(buffer + HEADER_SIZE_NUM_TOT_PACKETS, maxBufferSize);
				expected = maxBufferSize;
			}else{
#ifdef VERYDEBUG
				cout << "***carry on buffer" << carryonBufferSize << endl;
				cout<<"framennum in temochar:"<<((((uint32_t)(*((uint32_t*)tempchar)))
						& (frameIndexMask)) >> frameIndexOffset)<<endl;
				cout <<"temochar packet:"<< ((((uint32_t)(*((uint32_t*)(tempchar)))))
											& (packetIndexMask)) << endl;
#endif
				//if there is a packet from previous buffer, copy it and listen to n less frame
				memcpy(buffer + HEADER_SIZE_NUM_TOT_PACKETS, tempchar, carryonBufferSize);
				rc = udpSocket->ReceiveDataOnly((buffer + HEADER_SIZE_NUM_TOT_PACKETS + carryonBufferSize),maxBufferSize - carryonBufferSize);
				expected = maxBufferSize - carryonBufferSize;
			}

#ifdef VERYDEBUG
			cout << "*** rc:" << dec << rc << endl;
			cout << "*** expected:" << dec << expected << endl;
#endif
			//start indices
			//start of scan
			if((!measurementStarted) && (rc > 0)){
				//gotthard has +1 for frame number
				if ((myDetectorType == GOTTHARD) && (shortFrame == -1))
					startFrameIndex = (((((uint32_t)(*((uint32_t*)(buffer + HEADER_SIZE_NUM_TOT_PACKETS))))+1)
							& (frameIndexMask)) >> frameIndexOffset);
				else
					startFrameIndex = ((((uint32_t)(*((uint32_t*)(buffer+HEADER_SIZE_NUM_TOT_PACKETS))))
							& (frameIndexMask)) >> frameIndexOffset);
				cout<<"startFrameIndex:"<<startFrameIndex<<endl;
				prevframenum=startFrameIndex;
				measurementStarted = true;
				//start of acquisition
				if(!acqStarted){
					startAcquisitionIndex=startFrameIndex;
					currframenum = startAcquisitionIndex;
					acqStarted = true;
					cout<<"startAcquisitionIndex:"<<startAcquisitionIndex<<endl;
				}
			}


			//problem in receiving or end of acquisition
			if((rc < expected)||(rc <= 0)){
#ifdef VERYVERBOSE
				cerr << "recvfrom() failed:"<<endl;
#endif
				if(status != TRANSMITTING){
					cout<<"*** shoule never be here********************************"<<endl;/**/
					fifoFree->push(buffer);
					exit(-1);
					continue;
				}
				//push the last buffer into fifo
				if(rc > 0){
					packetcount = (rc/onePacketSize);
#ifdef VERYDEBUG
					cout << "*** last packetcount:" << packetcount << endl;
#endif
					(*((uint16_t*)(buffer))) = packetcount;
					totalListeningFrameCount += packetcount;
					while(!fifo->push(buffer));
#ifdef VERYDEBUG
					cout << "*** last lbuf1:" << (void*)buffer << endl;
#endif
				}


				//push dummy buffer
				for(int i=0;i<numWriterThreads;++i){
					fifoFree->pop(buffer);
					(*((uint16_t*)(buffer))) = 0xFFFF;
					while(!fifo->push(buffer));
#ifdef VERYDEBUG
					cout << "pushed in dummy buffer:" << (void*)buffer << endl;
#endif
				}
				cout << "Total count listened to " << totalListeningFrameCount/packetsPerFrame << endl;
				pthread_mutex_lock(&status_mutex);
				listening_thread_running = 0;
				pthread_mutex_unlock(&(status_mutex));
				break;
			}


			//reset
			packetcount = packetsPerFrame * numJobsPerThread;
			carryonBufferSize = 0;


			//check if last packet valid and calculate packet count
			switch(myDetectorType){



			case MOENCH:
				lastpacketoffset = (((numJobsPerThread * packetsPerFrame - 1) * onePacketSize) + HEADER_SIZE_NUM_TOT_PACKETS);
#ifdef VERYDEBUG
				cout <<"first packet:"<< ((((uint32_t)(*((uint32_t*)(buffer+HEADER_SIZE_NUM_TOT_PACKETS))))) & (packetIndexMask)) << endl;
				cout <<"first header:"<< (((((uint32_t)(*((uint32_t*)(buffer+HEADER_SIZE_NUM_TOT_PACKETS))))) & (frameIndexMask)) >> frameIndexOffset) << endl;
				cout << "last packet offset:" << lastpacketoffset << endl;
				cout <<"last packet:"<< ((((uint32_t)(*((uint32_t*)(buffer+lastpacketoffset))))) & (packetIndexMask)) << endl;
				cout <<"last header:"<< (((((uint32_t)(*((uint32_t*)(buffer+lastpacketoffset))))) & (frameIndexMask)) >> frameIndexOffset) << endl;
#endif
				//moench last packet value is 0
				if( ((((uint32_t)(*((uint32_t*)(buffer+lastpacketoffset))))) & (packetIndexMask))){
					lastframeheader = ((((uint32_t)(*((uint32_t*)(buffer+lastpacketoffset))))) & (frameIndexMask)) >> frameIndexOffset;
					carryonBufferSize += onePacketSize;
					lastpacketoffset -= onePacketSize;
					--packetcount;
					while (lastframeheader == (((((uint32_t)(*((uint32_t*)(buffer+lastpacketoffset))))) & (frameIndexMask)) >> frameIndexOffset)){
						carryonBufferSize += onePacketSize;
						lastpacketoffset -= onePacketSize;
						--packetcount;
					}
					memcpy(tempchar, buffer+(lastpacketoffset+onePacketSize), carryonBufferSize);
#ifdef VERYDEBUG
					cout << "tempchar header:" << (((((uint32_t)(*((uint32_t*)(tempchar)))))
							& (frameIndexMask)) >> frameIndexOffset) << endl;
					cout <<"tempchar packet:"<< ((((uint32_t)(*((uint32_t*)(tempchar)))))
							& (packetIndexMask)) << endl;
#endif
			}
				break;



			default:
				if(shortFrame == -1){
					lastpacketoffset = (((numJobsPerThread * packetsPerFrame - 1) * onePacketSize) + HEADER_SIZE_NUM_TOT_PACKETS);
#ifdef VERYDEBUG
					cout << "last packet offset:" << lastpacketoffset << endl;
#endif

					if((packetsPerFrame -1) != ((((uint32_t)(*((uint32_t*)(buffer+lastpacketoffset))))+1) & (packetIndexMask))){
						memcpy(tempchar,buffer+lastpacketoffset, onePacketSize);
#ifdef VERYDEBUG
						cout << "tempchar header:" << (((((uint32_t)(*((uint32_t*)(tempchar))))+1)
								& (frameIndexMask)) >> frameIndexOffset) << endl;
#endif
						carryonBufferSize = onePacketSize;
						--packetcount;
					}
				}
#ifdef VERYDEBUG
			cout << "header:" << (((((uint32_t)(*((uint32_t*)(buffer + HEADER_SIZE_NUM_TOT_PACKETS))))+1)
										& (frameIndexMask)) >> frameIndexOffset) << endl;
#endif
				break;




			}
#ifdef VERYDEBUG
			cout << "*** packetcount:" << packetcount << " carryonbuffer:" << carryonBufferSize << endl;
#endif
			//write packet count and push
			(*((uint16_t*)(buffer))) = packetcount;
			totalListeningFrameCount += packetcount;
			while(!fifo->push(buffer));
#ifdef VERYDEBUG
			cout << "*** pushed into listening fifo" << endl;
#endif
		}

		sem_wait(&listensmp);

		//make sure its not exiting thread
		if(killListeningThread)
			pthread_exit(NULL);
	}

	return OK;
}















int slsReceiverFunctionList::startWriting(){
	int ithread = currentWriterThreadIndex;
#ifdef VERYVERBOSE
	cout << ithread << "In startWriting()" <<endl;
#endif

	thread_started = 1;

	int numpackets,tempframenum, nf;
	char* wbuf;
	char *data=new char[bufferSize];
	int iFrame = 0;
	int xmax=0,ymax=0;
	int ret;


	while(1){

		nf = 0;
		iFrame = 0;
		if(myDetectorType == MOENCH){
				xmax = MOENCH_PIXELS_IN_ONE_ROW-1;
				ymax = MOENCH_PIXELS_IN_ONE_ROW-1;
			}else{
				if(shortFrame == -1){
				xmax = GOTTHARD_PIXELS_IN_ROW-1;
				ymax = GOTTHARD_PIXELS_IN_COL-1;
				}else{
					xmax = GOTTHARD_SHORT_PIXELS_IN_ROW-1;
					ymax = GOTTHARD_SHORT_PIXELS_IN_COL-1;
				}
			}


		while((1<<ithread)&writerthreads_mask){
#ifdef VERYDEBUG
			cout << ithread << " ***waiting to pop out of fifo" << endl;
#endif
			//pop
			fifo->pop(wbuf);
			numpackets =	(uint16_t)(*((uint16_t*)wbuf));
#ifdef VERYDEBUG
			cout << ithread << " numpackets:" << dec << numpackets << endl;
			cout << ithread << " *** popped from fifo " << numpackets << endl;
#endif






			//last dummy packet
			if(numpackets == 0xFFFF){
#ifdef VERYDEBUG
				cout << ithread << " **********************popped last dummy frame:" << (void*)wbuf << endl;
#endif

				//free fifo
				while(!fifoFree->push(wbuf));
#ifdef VERYDEBUG
				cout << ithread  << " fifo freed:" << (void*)wbuf << endl;
#endif



				//all threads need to close file, reset mask and exit loop
				closeFile(ithread);
				pthread_mutex_lock(&status_mutex);
				writerthreads_mask^=(1<<ithread);
#ifdef VERYDEBUG
				cout << ithread << " Resetting mask of current thread. New Mask: " << writerthreads_mask << endl;
#endif
				pthread_mutex_unlock(&status_mutex);



				//only thread 0 needs to do this
				//check if all jobs are done and wait
				//change status to run finished
				if(ithread == 0){
					if(dataCompression){
						cout << "Waiting for jobs to be done.. current mask:" << hex << writerthreads_mask << endl;
						while(writerthreads_mask){
							/*cout << "." << flush;*/
							usleep(50000);
						}
						cout<<" Jobs Done!"<<endl;
					}
					//update status
					pthread_mutex_lock(&status_mutex);
					status = RUN_FINISHED;
					pthread_mutex_unlock(&(status_mutex));
					//report
					cout << "Status: Run Finished" << endl;
					cout << "Total Packets Caught:" << dec << totalPacketsCaught << endl;
					cout << "Total Frames Caught:"<< dec << (totalPacketsCaught/packetsPerFrame) << endl;
					//acquisition end
					if (acquisitionFinishedCallBack)
						acquisitionFinishedCallBack((totalPacketsCaught/packetsPerFrame), pAcquisitionFinished);

				}
				continue;
			}







			//for progress
			if ((myDetectorType == GOTTHARD) && (shortFrame == -1))
				tempframenum = (((((uint32_t)(*((uint32_t*)(wbuf + HEADER_SIZE_NUM_TOT_PACKETS))))+1)& (frameIndexMask)) >> frameIndexOffset);
			else
				tempframenum = ((((uint32_t)(*((uint32_t*)(wbuf + HEADER_SIZE_NUM_TOT_PACKETS))))& (frameIndexMask)) >> frameIndexOffset);

			if(numWriterThreads == 1)
				currframenum = tempframenum;
			else{
				pthread_mutex_lock(&progress_mutex);
				if(tempframenum > currframenum)
					currframenum = tempframenum;
				pthread_mutex_unlock(&progress_mutex);
			}
#ifdef VERYDEBUG
	cout << ithread << " tempframenum:" << dec << tempframenum << " curframenum:" << currframenum << endl;
#endif





			//without datacompression: write datacall back, or write data, free fifo
			if(!dataCompression){
				if (cbAction < DO_EVERYTHING)
					rawDataReadyCallBack(currframenum, wbuf, numpackets * onePacketSize, sfilefd, guiData,pRawDataReady);
				else if (numpackets > 0){
					writeToFile_withoutCompression(wbuf, numpackets);
				}
				//copy to gui
				copyFrameToGui(wbuf + HEADER_SIZE_NUM_TOT_PACKETS);

				while(!fifoFree->push(wbuf));
#ifdef VERYVERBOSE
				cout<<"buf freed:"<<(void*)wbuf<<endl;
#endif
			}





			//data compression
			else{
#if defined(MYROOT1) && defined(ALLFILE_DEBUG)
				writeToFile_withoutCompression(wbuf, numpackets);
#endif

				eventType thisEvent = PEDESTAL;
				int ndata;
				char* buff = 0;
				data = wbuf+ HEADER_SIZE_NUM_TOT_PACKETS;
				int ir, ic;
				int remainingsize = numpackets * onePacketSize;
				int np;
				int once = 0;
				double tot, tl, tr, bl, br, v;
				int xmin = 1, ymin = 1, ix, iy;


				while(buff = receiverdata[ithread]->findNextFrame(data,ndata,remainingsize  )){
					np = ndata/onePacketSize;

					//cout<<"buff framnum:"<<ithread <<":"<< ((((uint32_t)(*((uint32_t*)buff)))& (frameIndexMask)) >> frameIndexOffset)<<endl;

					if ((np == packetsPerFrame) && (buff!=NULL)){
						if(nf == 1000) cout << "Thread " << ithread << ": pedestal done " << endl;


						singlePhotonDet[ithread]->newFrame();

						//only for moench
						if(commonModeSubtractionEnable){
							for(ix = xmin - 1; ix < xmax+1; ix++){
								for(iy = ymin - 1; iy < ymax+1; iy++){
									thisEvent = singlePhotonDet[ithread]->getEventType(buff, ix, iy, 0);
								}
							}
						}


						for(ix = xmin - 1; ix < xmax+1; ix++)
							for(iy = ymin - 1; iy < ymax+1; iy++){
								thisEvent=singlePhotonDet[ithread]->getEventType(buff, ix, iy, commonModeSubtractionEnable);
								if (nf>1000) {
									tot=0;
									tl=0;
									tr=0;
									bl=0;
									br=0;
									if (thisEvent==PHOTON_MAX) {

										iFrame=receiverdata[ithread]->getFrameNumber(buff);
#ifdef MYROOT1
										myTree[ithread]->Fill();
										//cout << "Fill in event: frmNr: " << iFrame <<  " ix " << ix << " iy " << iy << " type " <<  thisEvent << endl;
#else
										pthread_mutex_lock(&write_mutex);
										if((enableFileWrite) && (sfilefd))
											singlePhotonDet[ithread]->writeCluster(sfilefd);
										pthread_mutex_unlock(&write_mutex);
#endif
									}
								}
							}

						nf++;
#ifndef ALLFILE
						pthread_mutex_lock(&progress_mutex);
						packetsInFile += packetsPerFrame;
						packetsCaught += packetsPerFrame;
						totalPacketsCaught += packetsPerFrame;
						if(packetsInFile >= maxPacketsPerFile)
							createNewFile();
						pthread_mutex_unlock(&progress_mutex);

#endif
						if(!once){
							copyFrameToGui(buff);
							once = 1;
						}
					}

					remainingsize -= ((buff + ndata) - data);
					data = buff + ndata;
					if(data > (wbuf + HEADER_SIZE_NUM_TOT_PACKETS + numpackets * onePacketSize) )
						cout <<" **************ERROR SHOULD NOT COME HERE, Error 142536!"<<endl;

				}

				while(!fifoFree->push(wbuf));
#ifdef VERYVERBOSE
				cout<<"buf freed:"<<(void*)wbuf<<endl;
#endif

			}
		}
#ifdef VERYVERBOSE
		cout << ithread << " gonna wait for 1st sem" << endl;
#endif
		//wait
		sem_wait(&writersmp[ithread]);
		if(killAllWritingThreads){
			cout << ithread << " good bye thread" << endl;
			closeFile(ithread);
			pthread_exit(NULL);
		}
#ifdef VERYVERBOSE
		cout << ithread << " got 1st post" << endl;
#endif


		if((1<<ithread)&createfile_mask){
			if(dataCompression){
#ifdef MYROOT1
				pthread_mutex_lock(&write_mutex);
				ret = createCompressionFile(ithread,0);
				pthread_mutex_unlock(&write_mutex);
				if(ret == FAIL)
					ret_createfile = FAIL;
#endif
			}else{
				ret = createNewFile();
				if(ret == FAIL)
					ret_createfile = FAIL;
			}

			//let tcp know
			pthread_mutex_lock(&status_mutex);
			createfile_mask^=(1<<ithread);
			pthread_mutex_unlock(&status_mutex);
		}


#ifdef VERYVERBOSE
		cout << ithread << " gonna wait for 2nd sem" << endl;
#endif
		//wait
		sem_wait(&writersmp[ithread]);
		if(killAllWritingThreads){
			cout << ithread << " Goodbye thread" << endl;
			closeFile(ithread);
			pthread_exit(NULL);
		}
#ifdef VERYVERBOSE
		cout << ithread << " got 2nd post" << endl;
#endif
	}


	return OK;
}













void slsReceiverFunctionList::writeToFile_withoutCompression(char* buf,int numpackets){
	int packetsToSave, offset,tempframenum,lastpacket;

	//file write
	if((enableFileWrite) && (sfilefd)){

		offset = HEADER_SIZE_NUM_TOT_PACKETS;
		while(numpackets > 0){

			//for progress and packet loss calculation(new files)
			if ((myDetectorType == GOTTHARD) && (shortFrame == -1))
				tempframenum = (((((uint32_t)(*((uint32_t*)(buf + HEADER_SIZE_NUM_TOT_PACKETS))))+1)& (frameIndexMask)) >> frameIndexOffset);
			else
				tempframenum = ((((uint32_t)(*((uint32_t*)(buf + HEADER_SIZE_NUM_TOT_PACKETS))))& (frameIndexMask)) >> frameIndexOffset);

			if(numWriterThreads == 1)
				currframenum = tempframenum;
			else{
				if(tempframenum > currframenum)
					currframenum = tempframenum;
			}
#ifdef VERYDEBUG
			cout << "tempframenum:" << dec << tempframenum << " curframenum:" << currframenum << endl;
#endif

			//lock
			if(numWriterThreads > 1)
				pthread_mutex_lock(&write_mutex);


			//to create new file when max reached
			packetsToSave = maxPacketsPerFile - packetsInFile;
			if(packetsToSave > numpackets)
				packetsToSave = numpackets;

			fwrite(buf+offset, 1, packetsToSave * onePacketSize, sfilefd);
			packetsInFile += packetsToSave;
			packetsCaught += packetsToSave;
			totalPacketsCaught += packetsToSave;


			//new file
			if(packetsInFile >= maxPacketsPerFile){
				//for packet loss
				lastpacket = (((packetsToSave - 1) * onePacketSize) + offset);
				if ((myDetectorType == GOTTHARD) && (shortFrame == -1))
					tempframenum = (((((uint32_t)(*((uint32_t*)(buf + lastpacket))))+1)& (frameIndexMask)) >> frameIndexOffset);
				else
					tempframenum = ((((uint32_t)(*((uint32_t*)(buf + lastpacket))))& (frameIndexMask)) >> frameIndexOffset);

				if(numWriterThreads == 1)
					currframenum = tempframenum;
				else{
					if(tempframenum > currframenum)
						currframenum = tempframenum;
				}
#ifdef VERYDEBUG
				cout << "tempframenum:" << dec << tempframenum << " curframenum:" << currframenum << endl;
#endif
				//create
				createNewFile();
			}

			//unlock
			if(numWriterThreads > 1)
				pthread_mutex_unlock(&write_mutex);


			offset += (packetsToSave * onePacketSize);
			numpackets -= packetsToSave;
		}

	}
	else{
		if(numWriterThreads > 1)
			pthread_mutex_lock(&write_mutex);
		packetsInFile += numpackets;
		packetsCaught += numpackets;
		totalPacketsCaught += numpackets;
		if(numWriterThreads > 1)
			pthread_mutex_unlock(&write_mutex);
	}



}


#endif
