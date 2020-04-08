#include "ClientInterface.h"
#include "FixedCapacityContainer.h"
#include "ServerSocket.h"
#include "sls_detector_exceptions.h"
#include "string_utils.h"
#include "versionAPI.h"
#include "ToString.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <sys/syscall.h>
#include <unistd.h>
#include <map>

using sls::RuntimeError;
using sls::SocketError;
using Interface = sls::ServerInterface;

ClientInterface::~ClientInterface() { 
    killTcpThread = true;
    // shut down tcp sockets
    if (server.get() != nullptr) {
        LOG(logINFO) << "Shutting down TCP Socket on port " << portNumber;
        server->shutDownSocket(); 
        LOG(logDEBUG) << "TCP Socket closed on port " << portNumber;
    }
    // shut down tcp thread
    tcpThread->join();
}

ClientInterface::ClientInterface(int portNumber)
    : myDetectorType(GOTTHARD), 
    portNumber(portNumber > 0 ? portNumber : DEFAULT_PORTNO + 2) {
    functionTable();
    // start up tcp thread
    tcpThread = sls::make_unique<std::thread>(&ClientInterface::startTCPServer, this);
}

int64_t ClientInterface::getReceiverVersion() { return APIRECEIVER; }

/***callback functions***/
void ClientInterface::registerCallBackStartAcquisition(
    int (*func)(std::string, std::string, uint64_t, uint32_t, void *), void *arg) {
    startAcquisitionCallBack = func;
    pStartAcquisition = arg;
}

void ClientInterface::registerCallBackAcquisitionFinished(
    void (*func)(uint64_t, void *), void *arg) {
    acquisitionFinishedCallBack = func;
    pAcquisitionFinished = arg;
}

void ClientInterface::registerCallBackRawDataReady(
    void (*func)(char *, char *, uint32_t, void *), void *arg) {
    rawDataReadyCallBack = func;
    pRawDataReady = arg;
}

void ClientInterface::registerCallBackRawDataModifyReady(
    void (*func)(char *, char *, uint32_t &, void *), void *arg) {
    rawDataModifyReadyCallBack = func;
    pRawDataReady = arg;
}

void ClientInterface::startTCPServer() {
    LOG(logINFOBLUE) << "Created [ TCP server Tid: " << syscall(SYS_gettid) << "]";
    LOG(logINFO) << "SLS Receiver starting TCP Server on port "
                      << portNumber << '\n';
    server = sls::make_unique<sls::ServerSocket>(portNumber);
    while (true) {
        LOG(logDEBUG1) << "Start accept loop";
        try {
            auto socket = server->accept();
            try {
                verifyLock();
                ret = decodeFunction(socket);
            } catch (const RuntimeError &e) {
                // We had an error needs to be sent to client
                char mess[MAX_STR_LENGTH]{};
                sls::strcpy_safe(mess, e.what());
                socket.Send(FAIL);
                socket.Send(mess);
            }
            // if tcp command was to exit server
            if (ret == GOODBYE) {
                break;
            }
        } catch (const RuntimeError &e) {
            LOG(logERROR) << "Accept failed";
        }
        // destructor to kill this thread
        if (killTcpThread) {
            break;
        }
    }

    if (receiver) {
        receiver->shutDownUDPSockets();
    }
    LOG(logINFOBLUE) << "Exiting [ TCP server Tid: " << syscall(SYS_gettid) << "]";
}

// clang-format off
int ClientInterface::functionTable(){
	flist[F_EXEC_RECEIVER_COMMAND]			=	&ClientInterface::exec_command;
	flist[F_EXIT_RECEIVER]					=	&ClientInterface::exit_server;
	flist[F_LOCK_RECEIVER]					=	&ClientInterface::lock_receiver;
	flist[F_GET_LAST_RECEIVER_CLIENT_IP]	=	&ClientInterface::get_last_client_ip;
	flist[F_SET_RECEIVER_PORT]				=	&ClientInterface::set_port;
	flist[F_GET_RECEIVER_VERSION]			=	&ClientInterface::get_version;
	flist[F_SETUP_RECEIVER]				    =	&ClientInterface::setup_receiver;
	flist[F_RECEIVER_SET_ROI]				=	&ClientInterface::set_roi;
	flist[F_RECEIVER_SET_NUM_FRAMES]        =   &ClientInterface::set_num_frames;  
	flist[F_SET_RECEIVER_NUM_TRIGGERS]      =   &ClientInterface::set_num_triggers;           
	flist[F_SET_RECEIVER_NUM_BURSTS]        =   &ClientInterface::set_num_bursts;         
	flist[F_SET_RECEIVER_NUM_ADD_STORAGE_CELLS] = &ClientInterface::set_num_add_storage_cells;                 
	flist[F_SET_RECEIVER_TIMING_MODE]       =   &ClientInterface::set_timing_mode;          
	flist[F_SET_RECEIVER_BURST_MODE]        =   &ClientInterface::set_burst_mode;  
	flist[F_RECEIVER_SET_NUM_ANALOG_SAMPLES]=   &ClientInterface::set_num_analog_samples;            
	flist[F_RECEIVER_SET_NUM_DIGITAL_SAMPLES]=  &ClientInterface::set_num_digital_samples;           
	flist[F_RECEIVER_SET_EXPTIME]           =   &ClientInterface::set_exptime;
	flist[F_RECEIVER_SET_PERIOD]            =   &ClientInterface::set_period;
	flist[F_RECEIVER_SET_SUB_EXPTIME]       =   &ClientInterface::set_subexptime;    
	flist[F_RECEIVER_SET_SUB_DEADTIME]      =   &ClientInterface::set_subdeadtime;    
	flist[F_SET_RECEIVER_DYNAMIC_RANGE]		= 	&ClientInterface::set_dynamic_range;
	flist[F_SET_RECEIVER_STREAMING_FREQUENCY] = 	&ClientInterface::set_streaming_frequency;
	flist[F_GET_RECEIVER_STREAMING_FREQUENCY] = 	&ClientInterface::get_streaming_frequency;
	flist[F_GET_RECEIVER_STATUS]			=	&ClientInterface::get_status;
	flist[F_START_RECEIVER]					=	&ClientInterface::start_receiver;
	flist[F_STOP_RECEIVER]					=	&ClientInterface::stop_receiver;
	flist[F_SET_RECEIVER_FILE_PATH]			=	&ClientInterface::set_file_dir;
	flist[F_GET_RECEIVER_FILE_PATH]			=	&ClientInterface::get_file_dir;
	flist[F_SET_RECEIVER_FILE_NAME]			=	&ClientInterface::set_file_name;
	flist[F_GET_RECEIVER_FILE_NAME]			=	&ClientInterface::get_file_name;
	flist[F_SET_RECEIVER_FILE_INDEX]		=	&ClientInterface::set_file_index;
	flist[F_GET_RECEIVER_FILE_INDEX]		=	&ClientInterface::get_file_index;
	flist[F_GET_RECEIVER_FRAME_INDEX]		=	&ClientInterface::get_frame_index;    
	flist[F_GET_RECEIVER_FRAMES_CAUGHT]		=	&ClientInterface::get_frames_caught;
    flist[F_GET_NUM_MISSING_PACKETS]		=	&ClientInterface::get_missing_packets;
	flist[F_SET_RECEIVER_FILE_WRITE]		=	&ClientInterface::set_file_write;
	flist[F_GET_RECEIVER_FILE_WRITE]		=	&ClientInterface::get_file_write;
	flist[F_SET_RECEIVER_MASTER_FILE_WRITE]	=	&ClientInterface::set_master_file_write;
	flist[F_GET_RECEIVER_MASTER_FILE_WRITE]	=	&ClientInterface::get_master_file_write;
	flist[F_SET_RECEIVER_OVERWRITE]		    = 	&ClientInterface::set_overwrite;
	flist[F_GET_RECEIVER_OVERWRITE]		    = 	&ClientInterface::get_overwrite;
	flist[F_ENABLE_RECEIVER_TEN_GIGA]		= 	&ClientInterface::enable_tengiga;
	flist[F_SET_RECEIVER_FIFO_DEPTH]		= 	&ClientInterface::set_fifo_depth;
	flist[F_RECEIVER_ACTIVATE]				= 	&ClientInterface::set_activate;
	flist[F_SET_RECEIVER_STREAMING]		    = 	&ClientInterface::set_streaming;
	flist[F_GET_RECEIVER_STREAMING]		    = 	&ClientInterface::get_streaming;
	flist[F_RECEIVER_STREAMING_TIMER]		= 	&ClientInterface::set_streaming_timer;
	flist[F_SET_FLIPPED_DATA_RECEIVER]		= 	&ClientInterface::set_flipped_data;
	flist[F_SET_RECEIVER_FILE_FORMAT]		= 	&ClientInterface::set_file_format;
	flist[F_GET_RECEIVER_FILE_FORMAT]		= 	&ClientInterface::get_file_format;
	flist[F_SET_RECEIVER_STREAMING_PORT]	= 	&ClientInterface::set_streaming_port;
	flist[F_GET_RECEIVER_STREAMING_PORT]	= 	&ClientInterface::get_streaming_port;
	flist[F_SET_RECEIVER_STREAMING_SRC_IP]	= 	&ClientInterface::set_streaming_source_ip;
	flist[F_GET_RECEIVER_STREAMING_SRC_IP]	= 	&ClientInterface::get_streaming_source_ip;
	flist[F_SET_RECEIVER_SILENT_MODE]		= 	&ClientInterface::set_silent_mode;
	flist[F_GET_RECEIVER_SILENT_MODE]		= 	&ClientInterface::get_silent_mode;
	flist[F_RESTREAM_STOP_FROM_RECEIVER]	= 	&ClientInterface::restream_stop;
	flist[F_SET_ADDITIONAL_JSON_HEADER]     =   &ClientInterface::set_additional_json_header;
	flist[F_GET_ADDITIONAL_JSON_HEADER]     =   &ClientInterface::get_additional_json_header;
    flist[F_RECEIVER_UDP_SOCK_BUF_SIZE]     =   &ClientInterface::set_udp_socket_buffer_size;
    flist[F_RECEIVER_REAL_UDP_SOCK_BUF_SIZE]=   &ClientInterface::get_real_udp_socket_buffer_size;
    flist[F_SET_RECEIVER_FRAMES_PER_FILE]	=   &ClientInterface::set_frames_per_file;
    flist[F_GET_RECEIVER_FRAMES_PER_FILE]	=   &ClientInterface::get_frames_per_file;
    flist[F_RECEIVER_CHECK_VERSION]			=   &ClientInterface::check_version_compatibility;
    flist[F_SET_RECEIVER_DISCARD_POLICY]	=   &ClientInterface::set_discard_policy;
    flist[F_GET_RECEIVER_DISCARD_POLICY]	=   &ClientInterface::get_discard_policy;
	flist[F_SET_RECEIVER_PADDING]		    =   &ClientInterface::set_padding_enable;
	flist[F_GET_RECEIVER_PADDING]		    =   &ClientInterface::get_padding_enable;
	flist[F_SET_RECEIVER_DEACTIVATED_PADDING] = &ClientInterface::set_deactivated_padding_enable;
	flist[F_GET_RECEIVER_DEACTIVATED_PADDING] = &ClientInterface::get_deactivated_padding_enable;
	flist[F_RECEIVER_SET_READOUT_MODE] 	    = 	&ClientInterface::set_readout_mode;
	flist[F_RECEIVER_SET_ADC_MASK]			=	&ClientInterface::set_adc_mask;
	flist[F_SET_RECEIVER_DBIT_LIST]			=	&ClientInterface::set_dbit_list;
	flist[F_GET_RECEIVER_DBIT_LIST]			=	&ClientInterface::get_dbit_list;
	flist[F_SET_RECEIVER_DBIT_OFFSET]		= 	&ClientInterface::set_dbit_offset;
	flist[F_GET_RECEIVER_DBIT_OFFSET]		= 	&ClientInterface::get_dbit_offset;
    flist[F_SET_RECEIVER_QUAD]			    = 	&ClientInterface::set_quad_type;
    flist[F_SET_RECEIVER_READ_N_LINES]      =   &ClientInterface::set_read_n_lines;
    flist[F_SET_RECEIVER_UDP_IP]            =   &ClientInterface::set_udp_ip;
	flist[F_SET_RECEIVER_UDP_IP2]           =   &ClientInterface::set_udp_ip2;
	flist[F_SET_RECEIVER_UDP_PORT]          =   &ClientInterface::set_udp_port;
	flist[F_SET_RECEIVER_UDP_PORT2]         =   &ClientInterface::set_udp_port2;
	flist[F_SET_RECEIVER_NUM_INTERFACES]    =   &ClientInterface::set_num_interfaces;
	flist[F_RECEIVER_SET_ADC_MASK_10G]		=	&ClientInterface::set_adc_mask_10g;
    flist[F_RECEIVER_SET_NUM_COUNTERS]      =   &ClientInterface::set_num_counters;
    flist[F_INCREMENT_FILE_INDEX]           =   &ClientInterface::increment_file_index;
    flist[F_SET_ADDITIONAL_JSON_PARAMETER]  =   &ClientInterface::set_additional_json_parameter;
	flist[F_GET_ADDITIONAL_JSON_PARAMETER]  =   &ClientInterface::get_additional_json_parameter;
	flist[F_GET_RECEIVER_PROGRESS]          =   &ClientInterface::get_progress;
      
	for (int i = NUM_DET_FUNCTIONS + 1; i < NUM_REC_FUNCTIONS ; i++) {
		LOG(logDEBUG1) << "function fnum: " << i << " (" <<
				getFunctionNameFromEnum((enum detFuncs)i) << ") located at " << flist[i];
	}

	return OK;
}
// clang-format on
int ClientInterface::decodeFunction(Interface &socket) {
    ret = FAIL;
    socket.Receive(fnum);
    if (fnum <= NUM_DET_FUNCTIONS || fnum >= NUM_REC_FUNCTIONS) {
        throw RuntimeError("Unrecognized Function enum " +
                           std::to_string(fnum) + "\n");
    } else {
        LOG(logDEBUG1) << "calling function fnum: " << fnum << " ("
                            << getFunctionNameFromEnum((enum detFuncs)fnum)
                            << ")";
        ret = (this->*flist[fnum])(socket);
        LOG(logDEBUG1)
            << "Function " << getFunctionNameFromEnum((enum detFuncs)fnum)
            << " finished";
    }
    return ret;
}

void ClientInterface::functionNotImplemented() {
    std::ostringstream os;
    os << "Function: " << getFunctionNameFromEnum((enum detFuncs)fnum)
       << ", is is not implemented for this detector";
    throw RuntimeError(os.str());
}

void ClientInterface::modeNotImplemented(const std::string &modename,
                                                   int mode) {
    std::ostringstream os;
    os << modename << " (" << mode << ") is not implemented for this detector";
    throw RuntimeError(os.str());
}

template <typename T>
void ClientInterface::validate(T arg, T retval, const std::string& modename,
                                         numberMode hex) {
    if (ret == OK && arg != -1 && retval != arg) {
        auto format = (hex == HEX) ? std::hex : std::dec;
        auto prefix = (hex == HEX) ? "0x" : "";
        std::ostringstream os;
        os << "Could not " << modename << ". Set " << prefix << format << arg
           << ", but read " << prefix << retval << '\n';
        throw RuntimeError(os.str());
    }
}

void ClientInterface::verifyLock() {
    if (lockedByClient && server->getThisClient() != server->getLockedBy()) {
        throw sls::SocketError("Receiver locked\n");
    }
}

void ClientInterface::verifyIdle(Interface &socket) {
    if (impl()->getStatus() != IDLE) {
        std::ostringstream oss;
        oss << "Can not execute " << getFunctionNameFromEnum((enum detFuncs)fnum) 
            << " when receiver is not idle";
        throw sls::SocketError(oss.str());
    }
}

int ClientInterface::exec_command(Interface &socket) {
    char cmd[MAX_STR_LENGTH]{};
    char retval[MAX_STR_LENGTH]{};
    socket.Receive(cmd);
    LOG(logINFO) << "Executing command (" << cmd << ")";
    const size_t tempsize = 256;
    std::array<char, tempsize> temp{};
    std::string sresult;
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw RuntimeError("Executing Command failed\n");
    } else {
        while (!feof(pipe.get())) {
            if (fgets(temp.data(), tempsize, pipe.get()) != nullptr)
                sresult += temp.data();
        }
        strncpy(retval, sresult.c_str(), MAX_STR_LENGTH);
        LOG(logINFO) << "Result of cmd (" << cmd << "):\n" << retval;
    }
    return socket.sendResult(retval);
}

int ClientInterface::exit_server(Interface &socket) {
    LOG(logINFO) << "Closing server";
    socket.Send(OK);
    return GOODBYE;
}

int ClientInterface::lock_receiver(Interface &socket) {
    auto lock = socket.Receive<int>();
    LOG(logDEBUG1) << "Locking Server to " << lock;
    if (lock >= 0) {
        if (!lockedByClient || (server->getLockedBy() == server->getThisClient())) {
            lockedByClient = lock;
            lock ? server->setLockedBy(server->getThisClient())
                 : server->setLockedBy(sls::IpAddr{});
        } else {
            throw RuntimeError("Receiver locked\n");
        }
    }
    return socket.sendResult(lockedByClient);
}

int ClientInterface::get_last_client_ip(Interface &socket) {
    return socket.sendResult(server->getLastClient());
}

int ClientInterface::set_port(Interface &socket) {
    auto p_number = socket.Receive<int>();
    if (p_number < 1024)
        throw RuntimeError("Port Number: " + std::to_string(p_number) +
                           " is too low (<1024)");

    LOG(logINFO) << "TCP port set to " << p_number << std::endl;
    auto new_server = sls::make_unique<sls::ServerSocket>(p_number);
    new_server->setLockedBy(server->getLockedBy());
    new_server->setLastClient(server->getThisClient());
    server = std::move(new_server);
    socket.sendResult(p_number);
    return OK;
}

int ClientInterface::get_version(Interface &socket) {
    return socket.sendResult(getReceiverVersion());
}

int ClientInterface::setup_receiver(Interface &socket) {
    auto arg = socket.Receive<rxParameters>();
    LOG(logINFO) 
        << "detType:" << arg.detType << std::endl
        << "multiSize.x:" << arg.multiSize.x << std::endl
        << "multiSize.y:" << arg.multiSize.y << std::endl
        << "detId:" << arg.detId << std::endl
        << "hostname:" << arg.hostname << std::endl
        << "udpInterfaces:" << arg.udpInterfaces << std::endl
        << "udp_dstport:" << arg.udp_dstport << std::endl
        << "udp_dstip:" << sls::IpAddr(arg.udp_dstip) << std::endl
        << "udp_dstmac:" << sls::MacAddr(arg.udp_dstmac) << std::endl
        << "udp_dstport2:" << arg.udp_dstport2 << std::endl
        << "udp_dstip2:" << sls::IpAddr(arg.udp_dstip2) << std::endl
        << "udp_dstmac2:" << sls::MacAddr(arg.udp_dstmac2) << std::endl
        << "frames:" << arg.frames << std::endl
        << "triggers:" << arg.triggers << std::endl
        << "bursts:" << arg.bursts << std::endl
        << "analogSamples:" << arg.analogSamples << std::endl
        << "digitalSamples:" << arg.digitalSamples << std::endl
        << "expTimeNs:" << arg.expTimeNs << std::endl
        << "periodNs:" << arg.periodNs << std::endl
        << "subExpTimeNs:" << arg.subExpTimeNs << std::endl
        << "subDeadTimeNs:" << arg.subDeadTimeNs << std::endl
        << "activate:" << arg.activate << std::endl
        << "quad:" << arg.quad << std::endl
        << "dynamicRange:" << arg.dynamicRange << std::endl
        << "timMode:" << arg.timMode << std::endl
        << "tenGiga:" << arg.tenGiga << std::endl
        << "roMode:" << arg.roMode << std::endl
        << "adcMask:" << arg.adcMask << std::endl
        << "adc10gMask:" << arg.adc10gMask << std::endl
        << "roi.xmin:" << arg.roi.xmin << std::endl
        << "roi.xmax:" << arg.roi.xmax << std::endl
        << "countermask:" << arg.countermask << std::endl
        << "burstType:" << arg.burstType << std::endl;


    // if object exists, verify unlocked and idle, else only verify lock
    // (connecting first time)
    if (receiver != nullptr) {
        verifyIdle(socket);
    }

    // basic setup
    setDetectorType(arg.detType);
    {
        int msize[2] = {arg.multiSize.x, arg.multiSize.y};
        impl()->setMultiDetectorSize(msize);
    }
    impl()->setDetectorPositionId(arg.detId);
    impl()->setDetectorHostname(arg.hostname);
    
    // udp setup
    sls::MacAddr retvals[2];
    if (arg.udp_dstmac == 0 && arg.udp_dstip != 0) {
        retvals[0] = setUdpIp(sls::IpAddr(arg.udp_dstip));
    }
    if (arg.udp_dstmac2 == 0 && arg.udp_dstip2 != 0) {
        retvals[1] = setUdpIp2(sls::IpAddr(arg.udp_dstip2));
    }
    impl()->setUDPPortNumber(arg.udp_dstport);
    impl()->setUDPPortNumber2(arg.udp_dstport2);
    if (myDetectorType == JUNGFRAU) {
        try {
            impl()->setNumberofUDPInterfaces(arg.udpInterfaces);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Failed to set number of interfaces to " + 
            std::to_string(arg.udpInterfaces));
        }
    }
    impl()->setUDPSocketBufferSize(0);

    // acquisition parameters
    impl()->setNumberOfFrames(arg.frames);
    impl()->setNumberOfTriggers(arg.triggers);
    if (myDetectorType == GOTTHARD) {
        impl()->setNumberOfBursts(arg.bursts);
    }
    if (myDetectorType == MOENCH || myDetectorType == CHIPTESTBOARD) {
        try {
            impl()->setNumberofAnalogSamples(arg.analogSamples);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Could not set num analog samples to " + 
            std::to_string(arg.analogSamples) + 
            " due to fifo structure memory allocation.");
        }
    }
    if (myDetectorType == CHIPTESTBOARD) {
        try {
            impl()->setNumberofDigitalSamples(arg.digitalSamples);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Could not set num digital samples to " 
            + std::to_string(arg.analogSamples) + 
            " due to fifo structure memory allocation.");
        }
    }
    impl()->setAcquisitionTime(arg.expTimeNs);
    impl()->setAcquisitionPeriod(arg.periodNs);
    if (myDetectorType == EIGER) {
        impl()->setSubExpTime(arg.subExpTimeNs);
        impl()->setSubPeriod(arg.subExpTimeNs + arg.subDeadTimeNs);
        impl()->setActivate(static_cast<bool>(arg.activate));
        try {
            impl()->setQuad(arg.quad == 0 ? false : true);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Could not set quad to " + 
            std::to_string(arg.quad) + 
            " due to fifo strucutre memory allocation");
        }
    }
    if (myDetectorType == EIGER || myDetectorType == MYTHEN3) {
        try {
            impl()->setDynamicRange(arg.dynamicRange);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Could not set dynamic range. Could not allocate "
            "memory for fifo or could not start listening/writing threads");
        }
    }
    impl()->setTimingMode(arg.timMode);
    if (myDetectorType == EIGER || myDetectorType == MOENCH || 
        myDetectorType == CHIPTESTBOARD) {
        try {
            impl()->setTenGigaEnable(arg.tenGiga);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Could not set 10GbE.");
        } 
    }
    if (myDetectorType == CHIPTESTBOARD) {
        try {
            impl()->setReadoutMode(arg.roMode);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Could not set read out mode "
            "due to fifo memory allocation.");
        }
    }
    if (myDetectorType == CHIPTESTBOARD || myDetectorType == MOENCH) {
        try {
            impl()->setADCEnableMask(arg.adcMask);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Could not set adc enable mask "
            "due to fifo memory allcoation");
        }
        try {
            impl()->setTenGigaADCEnableMask(arg.adc10gMask);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Could not set 10Gb adc enable mask "
            "due to fifo memory allcoation");
        }
    }
    if (myDetectorType == GOTTHARD) {
        try {
            impl()->setROI(arg.roi);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Could not set ROI");
        }
    }
    if (myDetectorType == MYTHEN3) {
        int ncounters = __builtin_popcount(arg.countermask);
        impl()->setNumberofCounters(ncounters);
    }
    if (myDetectorType == GOTTHARD) {
        impl()->setBurstMode(arg.burstType);
    }

    return socket.sendResult(retvals);
}

void ClientInterface::setDetectorType(detectorType arg) {
    switch (arg) {
    case GOTTHARD:
    case EIGER:
    case CHIPTESTBOARD:
    case MOENCH:
    case JUNGFRAU:
    case MYTHEN3:
    case GOTTHARD2:
        break;
    default:
        throw RuntimeError("Unknown detector type: " + std::to_string(arg));
        break;
    }

    try {
        myDetectorType = GENERIC;
        receiver = sls::make_unique<Implementation>(arg);
        myDetectorType = arg;
    } catch (...) {
        throw RuntimeError("Could not set detector type");
    }

    // callbacks after (in setdetectortype, the object is reinitialized)
    if (startAcquisitionCallBack != nullptr)
        impl()->registerCallBackStartAcquisition(startAcquisitionCallBack,
                                                    pStartAcquisition);
    if (acquisitionFinishedCallBack != nullptr)
        impl()->registerCallBackAcquisitionFinished(
            acquisitionFinishedCallBack, pAcquisitionFinished);
    if (rawDataReadyCallBack != nullptr)
        impl()->registerCallBackRawDataReady(rawDataReadyCallBack,
                                                pRawDataReady);
    if (rawDataModifyReadyCallBack != nullptr)
        impl()->registerCallBackRawDataModifyReady(
            rawDataModifyReadyCallBack, pRawDataReady);
}

int ClientInterface::set_roi(Interface &socket) {
    static_assert(sizeof(ROI) == 2 * sizeof(int), "ROI not packed");
    ROI arg;
    socket.Receive(arg);
    LOG(logDEBUG1) << "Set ROI: [" << arg.xmin << ", " << arg.xmax << "]";

    if (myDetectorType != GOTTHARD)
        functionNotImplemented();

    verifyIdle(socket);
    try {
        impl()->setROI(arg);
    } catch(const RuntimeError &e) {
        throw RuntimeError("Could not set ROI");
    }
    return socket.Send(OK);
}

int ClientInterface::set_num_frames(Interface &socket) {
    auto value = socket.Receive<int64_t>();
    if (value <= 0) {
        throw RuntimeError("Invalid number of frames " + 
            std::to_string(value));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting num frames to " << value;
    impl()->setNumberOfFrames(value);
    int64_t retval = impl()->getNumberOfFrames();
    validate(value, retval, "set number of frames", DEC);
    return socket.Send(OK);
}

int ClientInterface::set_num_triggers(Interface &socket) {
    auto value = socket.Receive<int64_t>();
    if (value <= 0) {
        throw RuntimeError("Invalid number of triggers " + 
            std::to_string(value));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting num triggers to " << value;
    impl()->setNumberOfTriggers(value);
    int64_t retval = impl()->getNumberOfTriggers();
    validate(value, retval, "set number of triggers", DEC);
    return socket.Send(OK);
}

int ClientInterface::set_num_bursts(Interface &socket) {
    auto value = socket.Receive<int64_t>();
    if (value <= 0) {
        throw RuntimeError("Invalid number of bursts " + 
            std::to_string(value));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting num bursts to " << value;
    impl()->setNumberOfBursts(value);
    int64_t retval = impl()->getNumberOfBursts();
    validate(value, retval, "set number of bursts", DEC);
    return socket.Send(OK);
}

int ClientInterface::set_num_add_storage_cells(Interface &socket) {
    auto value = socket.Receive<int>();
    if (value < 0) {
        throw RuntimeError("Invalid number of additional storage cells " + 
            std::to_string(value));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting num additional storage cells to " << value;
    impl()->setNumberOfAdditionalStorageCells(value);
    int retval = impl()->getNumberOfAdditionalStorageCells();
    validate(value, retval, "set number of additional storage cells", DEC);
    return socket.Send(OK);
}

int ClientInterface::set_timing_mode(Interface &socket) {
    auto value = socket.Receive<int>();
    if (value < 0 || value >= NUM_TIMING_MODES) {    
        throw RuntimeError("Invalid timing mode " + 
            std::to_string(value));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting timing mode to " << value;
    impl()->setTimingMode(static_cast<timingMode>(value));
    int retval = impl()->getTimingMode();
    validate(value, retval, "set timing mode", DEC);
    return socket.Send(OK);
}

int ClientInterface::set_burst_mode(Interface &socket) {
    auto value = socket.Receive<int>();
    if (value < 0 || value >= NUM_BURST_MODES) {    
        throw RuntimeError("Invalid burst mode " + 
            std::to_string(value));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting burst mode to " << value;
    impl()->setBurstMode(static_cast<burstMode>(value));
    int retval = impl()->getBurstMode();
    validate(value, retval, "set burst mode", DEC);
    return socket.Send(OK);
}

int ClientInterface::set_num_analog_samples(Interface &socket) {
    auto value = socket.Receive<int>();
    LOG(logDEBUG1) << "Setting num analog samples to " << value;
    if (myDetectorType != CHIPTESTBOARD && myDetectorType != MOENCH) {
        functionNotImplemented();
    }    
    try {
        impl()->setNumberofAnalogSamples(value);
    } catch(const RuntimeError &e) {
        throw RuntimeError("Could not set num analog samples to " + std::to_string(value) + " due to fifo structure memory allocation.");
    }
    return socket.Send(OK);
}

int ClientInterface::set_num_digital_samples(Interface &socket) {
    auto value = socket.Receive<int>();
    LOG(logDEBUG1) << "Setting num digital samples to " << value;
    if (myDetectorType != CHIPTESTBOARD) {
        functionNotImplemented();
    }    
    try {
        impl()->setNumberofDigitalSamples(value);
    } catch(const RuntimeError &e) {
        throw RuntimeError("Could not set num digital samples to " + std::to_string(value) + " due to fifo structure memory allocation.");
    }
    return socket.Send(OK);
}

int ClientInterface::set_exptime(Interface &socket) {
    auto value = socket.Receive<int64_t>();
    LOG(logDEBUG1) << "Setting exptime to " << value << "ns";
    impl()->setAcquisitionTime(value);
    return socket.Send(OK);
}

int ClientInterface::set_period(Interface &socket) {
    auto value = socket.Receive<int64_t>();
    LOG(logDEBUG1) << "Setting period to " << value << "ns";
    impl()->setAcquisitionPeriod(value);
    return socket.Send(OK);
}

int ClientInterface::set_subexptime(Interface &socket) {
    auto value = socket.Receive<int64_t>();
    LOG(logDEBUG1) << "Setting period to " << value << "ns";
    uint64_t subdeadtime = impl()->getSubPeriod() - impl()->getSubExpTime();
    impl()->setSubExpTime(value);
    impl()->setSubPeriod(impl()->getSubExpTime() + subdeadtime);
    return socket.Send(OK);
}

int ClientInterface::set_subdeadtime(Interface &socket) {
    auto value = socket.Receive<int64_t>();
    LOG(logDEBUG1) << "Setting sub deadtime to " << value << "ns";
    impl()->setSubPeriod(value + impl()->getSubExpTime());
    LOG(logDEBUG1) << "Setting sub period to " << impl()->getSubPeriod() << "ns";    
    return socket.Send(OK);
}

int ClientInterface::set_dynamic_range(Interface &socket) {
    auto dr = socket.Receive<int>();
    if (dr >= 0) {
        verifyIdle(socket);
        LOG(logDEBUG1) << "Setting dynamic range: " << dr;
        bool exists = false;
        switch(myDetectorType) {
        case EIGER:
            if (dr == 4 || dr == 8 || dr == 16 || dr == 32) {
                exists = true;
            }
            break;
        case MYTHEN3:
            if (dr == 1 || dr == 4 || dr == 16 || dr == 32) {
                exists = true;
            }
            break;
        default:
            if (dr == 16)  {
                exists = true;
            }
            break;
        }
        if (!exists) {
            modeNotImplemented("Dynamic range", dr);
        } else {
            try {
                impl()->setDynamicRange(dr);
            } catch(const RuntimeError &e) {
                throw RuntimeError("Could not allocate memory for fifo or "
                                   "could not start listening/writing threads");
            }
        }
    }
    int retval = impl()->getDynamicRange();
    validate(dr, retval, "set dynamic range", DEC);
    LOG(logDEBUG1) << "dynamic range: " << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_streaming_frequency(Interface &socket) {
    auto index = socket.Receive<int>();
    if (index < 0) {
        throw RuntimeError("Invalid streaming frequency: "
            + std::to_string(index));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting streaming frequency: " << index;
    impl()->setStreamingFrequency(index);
    
    int retval = impl()->getStreamingFrequency();
    validate(index, retval, "set streaming frequency", DEC);
    return socket.Send(OK);
}

int ClientInterface::get_streaming_frequency(Interface &socket) {
    int retval = impl()->getStreamingFrequency();
    LOG(logDEBUG1) << "streaming freq:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::get_status(Interface &socket) {
    auto retval = impl()->getStatus();
    LOG(logDEBUG1) << "Status:" << sls::ToString(retval);
    return socket.sendResult(retval);
}

int ClientInterface::start_receiver(Interface &socket) {
    if (impl()->getStatus() == IDLE) {
        LOG(logDEBUG1) << "Starting Receiver";
        impl()->startReceiver();
    }
    return socket.Send(OK);
}

int ClientInterface::stop_receiver(Interface &socket) {
    auto arg = socket.Receive<int>();
    if (impl()->getStatus() == RUNNING) {
        LOG(logDEBUG1) << "Stopping Receiver";
        impl()->setStoppedFlag(static_cast<bool>(arg));
        impl()->stopReceiver();
    }
    auto s = impl()->getStatus();
    if (s != IDLE)
        throw RuntimeError("Could not stop receiver. It as it is: " +
                           sls::ToString(s));

    return socket.Send(OK);
}

int ClientInterface::set_file_dir(Interface &socket) {
    char fPath[MAX_STR_LENGTH]{};
    char retval[MAX_STR_LENGTH]{};
    socket.Receive(fPath);

    if (strlen(fPath) == 0) {
        throw RuntimeError("Cannot set empty file path");
    }
    if(fPath[0] != '/')
        throw RuntimeError("Receiver path needs to be absolute path");
    LOG(logDEBUG1) << "Setting file path: " << fPath;
    impl()->setFilePath(fPath);
    
    std::string s = impl()->getFilePath();
    sls::strcpy_safe(retval, s.c_str());
    if ((s.empty()) || (strlen(fPath) && strcasecmp(fPath, retval)))
        throw RuntimeError("Receiver file path does not exist");
    else
        LOG(logDEBUG1) << "file path:" << retval;

    return socket.Send(OK);
}

int ClientInterface::get_file_dir(Interface &socket) {
    char retval[MAX_STR_LENGTH]{};
    std::string s = impl()->getFilePath();
    sls::strcpy_safe(retval, s.c_str());
    LOG(logDEBUG1) << "file path:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_file_name(Interface &socket) {
    char fName[MAX_STR_LENGTH]{};
    char retval[MAX_STR_LENGTH]{};
    socket.Receive(fName);
    if (strlen(fName) == 0) {
        throw RuntimeError("Cannot set empty file name");
    }
    LOG(logDEBUG1) << "Setting file name: " << fName;
    impl()->setFileName(fName);
    
    std::string s = impl()->getFileName();
    sls::strcpy_safe(retval, s.c_str());
    LOG(logDEBUG1) << "file name:" << retval;
    return socket.Send(OK);
}

int ClientInterface::get_file_name(Interface &socket) {
    char retval[MAX_STR_LENGTH]{};
    std::string s = impl()->getFileName();
    sls::strcpy_safe(retval, s.c_str());
    LOG(logDEBUG1) << "file name:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_file_index(Interface &socket) {
    auto index = socket.Receive<int64_t>();
    if (index < 0) {
        throw RuntimeError("Invalid file index: " 
            + std::to_string(index));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting file index: " << index;
    impl()->setFileIndex(index);

    int64_t retval = impl()->getFileIndex();
    validate(index, retval, "set file index", DEC);
    LOG(logDEBUG1) << "file index:" << retval;
    return socket.Send(OK);
}

int ClientInterface::get_file_index(Interface &socket) {
    int64_t retval = impl()->getFileIndex();
    LOG(logDEBUG1) << "file index:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::get_frame_index(Interface &socket) {
    uint64_t retval = impl()->getAcquisitionIndex();
    LOG(logDEBUG1) << "frame index:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::get_missing_packets(Interface &socket) {
    std::vector<uint64_t> m = impl()->getNumMissingPackets();
    LOG(logDEBUG1) << "missing packets:" << sls::ToString(m);
    int retvalsize = m.size();
    uint64_t retval[retvalsize];
    std::copy(std::begin(m), std::end(m), retval);
    socket.Send(OK);
    socket.Send(&retvalsize, sizeof(retvalsize));
    socket.Send(retval, sizeof(retval));
    return OK;
}

int ClientInterface::get_frames_caught(Interface &socket) {
    int64_t retval = impl()->getFramesCaught();
    LOG(logDEBUG1) << "frames caught:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_file_write(Interface &socket) {
    auto enable = socket.Receive<int>();
    if (enable < 0) {
        throw RuntimeError("Invalid file write enable: " + std::to_string(enable));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting File write enable:" << enable;
    impl()->setFileWriteEnable(enable);

    int retval = impl()->getFileWriteEnable();
    validate(enable, retval, "set file write enable", DEC);
    LOG(logDEBUG1) << "file write enable:" << retval;
    return socket.Send(OK);
}

int ClientInterface::get_file_write(Interface &socket) {
    int retval = impl()->getFileWriteEnable();
    LOG(logDEBUG1) << "file write enable:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_master_file_write(Interface &socket) {
    auto enable = socket.Receive<int>();
    if (enable < 0) {
        throw RuntimeError("Invalid master file write enable: "
            + std::to_string(enable));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting Master File write enable:" << enable;
    impl()->setMasterFileWriteEnable(enable);
    
    int retval = impl()->getMasterFileWriteEnable();
    validate(enable, retval, "set master file write enable", DEC);
    LOG(logDEBUG1) << "master file write enable:" << retval;
    return socket.Send(OK);
}

int ClientInterface::get_master_file_write(Interface &socket) {
    int retval = impl()->getMasterFileWriteEnable();
    LOG(logDEBUG1) << "master file write enable:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_overwrite(Interface &socket) {
    auto index = socket.Receive<int>();
    if (index < 0) {
        throw RuntimeError("Invalid over write enable: "
            + std::to_string(index));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting File overwrite enable:" << index;
    impl()->setOverwriteEnable(index);
    
    int retval = impl()->getOverwriteEnable();
    validate(index, retval, "set file overwrite enable", DEC);
    LOG(logDEBUG1) << "file overwrite enable:" << retval;
    return socket.Send(OK);
}

int ClientInterface::get_overwrite(Interface &socket) {
    int retval = impl()->getOverwriteEnable();
    LOG(logDEBUG1) << "file overwrite enable:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::enable_tengiga(Interface &socket) {
    auto val = socket.Receive<int>();
    if (myDetectorType != EIGER && myDetectorType != CHIPTESTBOARD &&
        myDetectorType != MOENCH)
        functionNotImplemented();

    if (val >= 0) {
        verifyIdle(socket);
        LOG(logDEBUG1) << "Setting 10GbE:" << val;
        try {
            impl()->setTenGigaEnable(val);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Could not set 10GbE." );
        }
    }
    int retval = impl()->getTenGigaEnable();
    validate(val, retval, "set 10GbE", DEC);
    LOG(logDEBUG1) << "10Gbe:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_fifo_depth(Interface &socket) {
    auto value = socket.Receive<int>();
    if (value >= 0) {
        verifyIdle(socket);
        LOG(logDEBUG1) << "Setting fifo depth:" << value;
        try {
            impl()->setFifoDepth(value);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Could not set fifo depth due to fifo structure memory allocation.");
        }
    }
    int retval = impl()->getFifoDepth();
    validate(value, retval, std::string("set fifo depth"), DEC);
    LOG(logDEBUG1) << "fifo depth:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_activate(Interface &socket) {
    auto enable = socket.Receive<int>();
    if (myDetectorType != EIGER)
        functionNotImplemented();

    if (enable >= 0) {
        verifyIdle(socket);
        LOG(logDEBUG1) << "Setting activate:" << enable;
        impl()->setActivate(static_cast<bool>(enable));
    }
    auto retval = static_cast<int>(impl()->getActivate());
    validate(enable, retval, "set activate", DEC);
    LOG(logDEBUG1) << "Activate: " << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_streaming(Interface &socket) {
    auto index = socket.Receive<int>();
    if (index < 0) {
        throw RuntimeError("Invalid streaming enable: "
            + std::to_string(index));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting data stream enable:" << index;
    try {
        impl()->setDataStreamEnable(index);
    } catch(const RuntimeError &e) {
        throw RuntimeError("Could not set data stream enable to " + std::to_string(index));
    }
    
    auto retval = static_cast<int>(impl()->getDataStreamEnable());
    validate(index, retval, "set data stream enable", DEC);
    LOG(logDEBUG1) << "data streaming enable:" << retval;
    return socket.Send(OK);
}

int ClientInterface::get_streaming(Interface &socket) {
    auto retval = static_cast<int>(impl()->getDataStreamEnable());
    LOG(logDEBUG1) << "data streaming enable:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_streaming_timer(Interface &socket) {
    auto index = socket.Receive<int>();
    if (index >= 0) {
        verifyIdle(socket);
        LOG(logDEBUG1) << "Setting streaming timer:" << index;
        impl()->setStreamingTimer(index);
    }
    int retval = impl()->getStreamingTimer();
    validate(index, retval, "set data stream timer", DEC);
    LOG(logDEBUG1) << "Streaming timer:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_flipped_data(Interface &socket) {
    auto arg = socket.Receive<int>();

    if (myDetectorType != EIGER)
        functionNotImplemented();

    if (arg >= 0) {
        verifyIdle(socket);
        LOG(logDEBUG1) << "Setting flipped data:" << arg;
        impl()->setFlippedDataX(arg);
    }
    int retval = impl()->getFlippedDataX();
    validate(arg, retval, std::string("set flipped data"), DEC);
    LOG(logDEBUG1) << "Flipped Data:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_file_format(Interface &socket) {
    fileFormat f = GET_FILE_FORMAT;
    socket.Receive(f);
    if (f < 0 || f > NUM_FILE_FORMATS) {
        throw RuntimeError("Invalid file format: "
            + std::to_string(f));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting file format:" << f;
    impl()->setFileFormat(f);
    
    auto retval = impl()->getFileFormat();
    validate(f, retval, "set file format", DEC);
    LOG(logDEBUG1) << "File Format: " << retval;
    return socket.Send(OK);
}

int ClientInterface::get_file_format(Interface &socket) {
    auto retval = impl()->getFileFormat();
    LOG(logDEBUG1) << "File Format: " << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_streaming_port(Interface &socket) {
    auto port = socket.Receive<int>();
    if (port < 0) {
        throw RuntimeError("Invalid zmq port " + std::to_string(port));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting streaming port:" << port;
    impl()->setStreamingPort(port);

    int retval = impl()->getStreamingPort();
    validate(port, retval, "set streaming port", DEC);
    LOG(logDEBUG1) << "streaming port:" << retval;
    return socket.Send(OK);
}

int ClientInterface::get_streaming_port(Interface &socket) {
    int retval = impl()->getStreamingPort();
    LOG(logDEBUG1) << "streaming port:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_streaming_source_ip(Interface &socket) {
    sls::IpAddr arg;
    socket.Receive(arg);
    if (arg == 0) { 
        throw RuntimeError("Invalid zmq ip " + arg.str());
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting streaming source ip:" << arg;
    impl()->setStreamingSourceIP(arg);
    
    sls::IpAddr retval = impl()->getStreamingSourceIP();
    LOG(logDEBUG1) << "streaming IP:" << retval;
    if (retval != arg && arg != 0) {
        std::ostringstream os;
        os << "Could not set streaming ip. Set " << arg
           << ", but read " << retval << '\n';
        throw RuntimeError(os.str());
    }
    return socket.Send(OK);
}

int ClientInterface::get_streaming_source_ip(Interface &socket) {
    sls::IpAddr retval = impl()->getStreamingSourceIP();
    LOG(logDEBUG1) << "streaming IP:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_silent_mode(Interface &socket) {
    auto value = socket.Receive<int>();
    if (value < 0) {
        throw RuntimeError("Invalid silent mode: "
            + std::to_string(value));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting silent mode:" << value;
    impl()->setSilentMode(value);
    
    auto retval = static_cast<int>(impl()->getSilentMode());
    validate(value, retval, "set silent mode", DEC);
    LOG(logDEBUG1) << "silent mode:" << retval;
    return socket.Send(OK);
}

int ClientInterface::get_silent_mode(Interface &socket) {
    auto retval = static_cast<int>(impl()->getSilentMode());
    LOG(logDEBUG1) << "silent mode:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::restream_stop(Interface &socket) {
    verifyIdle(socket);
    if (!impl()->getDataStreamEnable()) {
        throw RuntimeError(
            "Could not restream stop packet as data Streaming is disabled");
    } else {
        LOG(logDEBUG1) << "Restreaming stop";
        impl()->restreamStop();
    }
    return socket.Send(OK);
}

int ClientInterface::set_additional_json_header(Interface &socket) {
    std::map<std::string, std::string> json;
    int size = socket.Receive<int>();
    if (size > 0) {
        char args[size * 2][SHORT_STR_LENGTH];
        memset(args, 0, sizeof(args));
        socket.Receive(args, sizeof(args));
        for (int i = 0; i < size; ++i) {
            json[args[2 * i]] = args[2 * i + 1];
        }    
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting additional json header: " << sls::ToString(json);
    impl()->setAdditionalJsonHeader(json);
    return socket.Send(OK);
}

int ClientInterface::get_additional_json_header(Interface &socket) {
    std::map<std::string, std::string> json = impl()->getAdditionalJsonHeader();
    LOG(logDEBUG1) << "additional json header:" << sls::ToString(json);
    int size = json.size();
    socket.sendResult(size);
    if (size > 0) {
        char retvals[size * 2][SHORT_STR_LENGTH];
        memset(retvals, 0, sizeof(retvals));
        int iarg = 0;
        for (auto & it : json) {
            sls::strcpy_safe(retvals[iarg], it.first.c_str());
            sls::strcpy_safe(retvals[iarg + 1], it.second.c_str());
            iarg += 2;
        }
        socket.Send(retvals, sizeof(retvals));
    }
    return OK;
}

int ClientInterface::set_udp_socket_buffer_size(Interface &socket) {
    auto index = socket.Receive<int64_t>();
    if (index >= 0) {
        verifyIdle(socket);
        LOG(logDEBUG1) << "Setting UDP Socket Buffer size: " << index;
        impl()->setUDPSocketBufferSize(index);
    }
    int64_t retval = impl()->getUDPSocketBufferSize();
    if (index != 0)
        validate(index, retval,
                 "set udp socket buffer size (No CAP_NET_ADMIN privileges?)",
                 DEC);
    LOG(logDEBUG1) << "UDP Socket Buffer Size:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::get_real_udp_socket_buffer_size(
    Interface &socket) {
    auto size = impl()->getActualUDPSocketBufferSize();
    LOG(logDEBUG1) << "Actual UDP socket size :" << size;
    return socket.sendResult(size);
}

int ClientInterface::set_frames_per_file(Interface &socket) {
    auto index = socket.Receive<int>();
    if (index < 0) {
        throw RuntimeError("Invalid frames per file: "
            + std::to_string(index));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting frames per file: " << index;
    impl()->setFramesPerFile(index);
    
    auto retval = static_cast<int>(impl()->getFramesPerFile());
    validate(index, retval, "set frames per file", DEC);
    LOG(logDEBUG1) << "frames per file:" << retval;
    return socket.Send(OK);
}

int ClientInterface::get_frames_per_file(Interface &socket) {
    auto retval = static_cast<int>(impl()->getFramesPerFile());
    LOG(logDEBUG1) << "frames per file:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::check_version_compatibility(Interface &socket) {
    auto arg = socket.Receive<int64_t>();
    LOG(logDEBUG1) << "Checking versioning compatibility with value "
                        << arg;
    int64_t client_requiredVersion = arg;
    int64_t rx_apiVersion = APIRECEIVER;
    int64_t rx_version = getReceiverVersion();

    if (rx_apiVersion > client_requiredVersion) {
        std::ostringstream os;
        os << "Incompatible versions.\n Client's receiver API Version: (0x"
           << std::hex << client_requiredVersion
           << "). Receiver API Version: (0x" << std::hex
           << ").\n Please update the client!\n";
        throw RuntimeError(os.str());
    } else if (client_requiredVersion > rx_version) {
        std::ostringstream os;
        os << "This receiver is incompatible.\n Receiver Version: (0x"
           << std::hex << rx_version << "). Client's receiver API Version: (0x"
           << std::hex << client_requiredVersion
           << ").\n Please update the receiver";
        throw RuntimeError(os.str());
    } else {
        LOG(logINFO) << "Compatibility with Client: Successful";
    }
    return socket.Send(OK);
}

int ClientInterface::set_discard_policy(Interface &socket) {
    auto index = socket.Receive<int>();
    if (index < 0 || index > NUM_DISCARD_POLICIES) {
        throw RuntimeError("Invalid discard policy " + std::to_string(index));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting frames discard policy: " << index;
    impl()->setFrameDiscardPolicy(static_cast<frameDiscardPolicy>(index));
    int retval = impl()->getFrameDiscardPolicy();
    validate(index, retval, "set discard policy", DEC);
    LOG(logDEBUG1) << "frame discard policy:" << retval;
    return socket.Send(OK);
}

int ClientInterface::get_discard_policy(Interface &socket) {
    int retval = impl()->getFrameDiscardPolicy();
    LOG(logDEBUG1) << "frame discard policy:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_padding_enable(Interface &socket) {
    auto index = socket.Receive<int>();
    if (index < 0) {
        throw RuntimeError("Invalid padding enable: " + std::to_string(index));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting frames padding enable: " << index;
    impl()->setFramePaddingEnable(static_cast<bool>(index));

    auto retval = static_cast<int>(impl()->getFramePaddingEnable());
    validate(index, retval, "set frame padding enable", DEC);
    LOG(logDEBUG1) << "Frame Padding Enable:" << retval;
    return socket.Send(OK);
}

int ClientInterface::get_padding_enable(Interface &socket) {
    auto retval = static_cast<int>(impl()->getFramePaddingEnable());
    LOG(logDEBUG1) << "Frame Padding Enable:" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_deactivated_padding_enable(
    Interface &socket) {
    auto enable = socket.Receive<int>();
    if (myDetectorType != EIGER) {
        functionNotImplemented();
    }
    if (enable < 0) {
        throw RuntimeError("Invalid Deactivated padding: " 
            + std::to_string(enable));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting deactivated padding enable: " << enable;
    impl()->setDeactivatedPadding(enable > 0);

    auto retval = static_cast<int>(impl()->getDeactivatedPadding());
    validate(enable, retval, "set deactivated padding enable", DEC);
    LOG(logDEBUG1) << "Deactivated Padding Enable: " << retval;
    return socket.Send(OK);
}

int ClientInterface::get_deactivated_padding_enable(
    Interface &socket) {
    if (myDetectorType != EIGER)
        functionNotImplemented();
    auto retval = static_cast<int>(impl()->getDeactivatedPadding());
    LOG(logDEBUG1) << "Deactivated Padding Enable: " << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_readout_mode(Interface &socket) {
    auto arg = socket.Receive<readoutMode>();

    if (myDetectorType != CHIPTESTBOARD)
        functionNotImplemented();

    if (arg >= 0) {
        verifyIdle(socket);
        LOG(logDEBUG1) << "Setting readout mode: " << arg;
        try {
            impl()->setReadoutMode(arg);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Could not set read out mode due to fifo memory allocation.");
        }
    }
    auto retval = impl()->getReadoutMode();
    validate(static_cast<int>(arg), static_cast<int>(retval),
             "set readout mode", DEC);
    LOG(logDEBUG1) << "Readout mode: " << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_adc_mask(Interface &socket) {
    auto arg = socket.Receive<uint32_t>();
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting 1Gb ADC enable mask: " << arg;
    try {
        impl()->setADCEnableMask(arg);
    } catch(const RuntimeError &e) {
        throw RuntimeError("Could not set adc enable mask due to fifo memory allcoation");
    }
    auto retval = impl()->getADCEnableMask();
    if (retval != arg) {
        std::ostringstream os;
        os << "Could not set 1Gb ADC enable mask. Set 0x" << std::hex << arg
           << " but read 0x" << std::hex << retval;
        throw RuntimeError(os.str());
    }
    LOG(logDEBUG1) << "1Gb ADC enable mask retval: " << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_dbit_list(Interface &socket) {
    sls::FixedCapacityContainer<int, MAX_RX_DBIT> args;
    socket.Receive(args);
    if (myDetectorType != CHIPTESTBOARD)
        functionNotImplemented();
    LOG(logDEBUG1) << "Setting DBIT list";
    for (auto &it : args) {
        LOG(logDEBUG1) << it << " ";
    }
    LOG(logDEBUG1) << "\n";
    verifyIdle(socket);
    impl()->setDbitList(args);
    return socket.Send(OK);
}

int ClientInterface::get_dbit_list(Interface &socket) {
    if (myDetectorType != CHIPTESTBOARD)
        functionNotImplemented();
    sls::FixedCapacityContainer<int, MAX_RX_DBIT> retval;
    retval = impl()->getDbitList();
    LOG(logDEBUG1) << "Dbit list size retval:" << retval.size();
    return socket.sendResult(retval);
}

int ClientInterface::set_dbit_offset(Interface &socket) {
    auto arg = socket.Receive<int>();
    if (myDetectorType != CHIPTESTBOARD)
        functionNotImplemented();
    if (arg < 0) {
        throw RuntimeError("Invalid dbit offset: "
            + std::to_string(arg));
    }
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting Dbit offset: " << arg;
    impl()->setDbitOffset(arg);
    
    int retval = impl()->getDbitOffset();
    validate(arg, retval, "set dbit offset", DEC);
    LOG(logDEBUG1) << "Dbit offset retval: " << retval;
    return socket.Send(OK);
}

int ClientInterface::get_dbit_offset(Interface &socket) {
    if (myDetectorType != CHIPTESTBOARD)
        functionNotImplemented();
    int retval = impl()->getDbitOffset();
    LOG(logDEBUG1) << "Dbit offset retval: " << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_quad_type(Interface &socket) {
    auto quadEnable = socket.Receive<int>();
    if (quadEnable >= 0) {
        verifyIdle(socket);
        LOG(logDEBUG1) << "Setting quad:" << quadEnable;
        try {
            impl()->setQuad(quadEnable == 0 ? false : true);
        } catch(const RuntimeError &e) {
            throw RuntimeError("Could not set quad to " + std::to_string(quadEnable) + " due to fifo strucutre memory allocation");
        }
    }
    int retval = impl()->getQuad() ? 1 : 0;
    validate(quadEnable, retval, "set quad", DEC);
    LOG(logDEBUG1) << "quad retval:" << retval;
    return socket.Send(OK);
}

int ClientInterface::set_read_n_lines(Interface &socket) {
    auto arg = socket.Receive<int>();
    if (arg >= 0) {
        verifyIdle(socket);
        LOG(logDEBUG1) << "Setting Read N Lines:" << arg;
        impl()->setReadNLines(arg);
    }
    int retval = impl()->getReadNLines();
    validate(arg, retval, "set read n lines", DEC);
    LOG(logDEBUG1) << "read n lines retval:" << retval;
    return socket.Send(OK);
}

sls::MacAddr ClientInterface::setUdpIp(sls::IpAddr arg) {
    // getting eth
    std::string eth = sls::IpToInterfaceName(arg.str());
    if (eth == "none") {
        throw RuntimeError("Failed to get udp ethernet interface from IP " + arg.str());
    }   
    if (eth.find('.') != std::string::npos) {
        eth = "";
        LOG(logERROR) << "Failed to get udp ethernet interface from IP " << arg << ". Got " << eth;
    }   
    impl()->setEthernetInterface(eth);
    if (myDetectorType == EIGER) {
        impl()->setEthernetInterface2(eth);
    }
    // get mac address
    auto retval = sls::InterfaceNameToMac(eth);
    if (retval == 0) {
        throw RuntimeError("Failed to get udp mac adddress to listen to (eth:" + eth + ", ip:" + arg.str() + ")\n");
    }
    return retval;
}

int ClientInterface::set_udp_ip(Interface &socket) {
    auto arg = socket.Receive<sls::IpAddr>();
    verifyIdle(socket);
    LOG(logINFO) << "Received UDP IP: " << arg;
    auto retval = setUdpIp(arg);
    LOG(logINFO) << "Receiver MAC Address: " << retval;
    return socket.sendResult(retval);
}

sls::MacAddr ClientInterface::setUdpIp2(sls::IpAddr arg) {
    // getting eth
    std::string eth = sls::IpToInterfaceName(arg.str());
    if (eth == "none") {
        throw RuntimeError("Failed to get udp ethernet interface2 from IP " + arg.str());
    }   
    if (eth.find('.') != std::string::npos) {
        eth = "";
        LOG(logERROR) << "Failed to get udp ethernet interface2 from IP " << arg << ". Got " << eth;
    }   
    impl()->setEthernetInterface2(eth);

    // get mac address
    auto retval = sls::InterfaceNameToMac(eth);
    if (retval == 0) {
        throw RuntimeError("Failed to get udp mac adddress2 to listen to (eth:" + eth + ", ip:" + arg.str() + ")\n");
    }
    return retval;
}

int ClientInterface::set_udp_ip2(Interface &socket) {
    auto arg = socket.Receive<sls::IpAddr>();
    verifyIdle(socket);
    if (myDetectorType != JUNGFRAU) {
        throw RuntimeError("UDP Destination IP2 not implemented for this detector");
    }
    LOG(logINFO) << "Received UDP IP2: " << arg;
    auto retval = setUdpIp2(arg);
    LOG(logINFO) << "Receiver MAC Address2: " << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_udp_port(Interface &socket) {
    auto arg = socket.Receive<int>();
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting UDP Port:" << arg;
    impl()->setUDPPortNumber(arg);
    return socket.Send(OK);
}

int ClientInterface::set_udp_port2(Interface &socket) {
    auto arg = socket.Receive<int>();
    verifyIdle(socket);
    if (myDetectorType != JUNGFRAU && myDetectorType != EIGER) {
        throw RuntimeError("UDP Destination Port2 not implemented for this detector");
    }    
    LOG(logDEBUG1) << "Setting UDP Port:" << arg;
    impl()->setUDPPortNumber2(arg);
    return socket.Send(OK);
}

int ClientInterface::set_num_interfaces(Interface &socket) {
    auto arg = socket.Receive<int>();
    arg = (arg > 1 ? 2 : 1);
    verifyIdle(socket);
    if (myDetectorType != JUNGFRAU) {
        throw RuntimeError("Number of interfaces not implemented for this detector");
    }    
    LOG(logDEBUG1) << "Setting Number of UDP Interfaces:" << arg;
    try {
        impl()->setNumberofUDPInterfaces(arg);
    } catch(const RuntimeError &e) {
        throw RuntimeError("Failed to set number of interfaces to " + std::to_string(arg));
    }
    return socket.Send(OK);
}

int ClientInterface::set_adc_mask_10g(Interface &socket) {
    auto arg = socket.Receive<uint32_t>();
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting 10Gb ADC enable mask: " << arg;
    try {
        impl()->setTenGigaADCEnableMask(arg);
    } catch(const RuntimeError &e) {
        throw RuntimeError("Could not set 10Gb adc enable mask due to fifo memory allcoation");
    }
    auto retval = impl()->getTenGigaADCEnableMask();
    if (retval != arg) {
        std::ostringstream os;
        os << "Could not 10gb ADC enable mask. Set 0x" << std::hex << arg
           << " but read 0x" << std::hex << retval;
        throw RuntimeError(os.str());
    }
    LOG(logDEBUG1) << "10Gb ADC enable mask retval: " << retval;
    return socket.sendResult(retval);
}

int ClientInterface::set_num_counters(Interface &socket) {
    auto arg = socket.Receive<int>();
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting counters: " << arg;
    impl()->setNumberofCounters(arg);
    return socket.Send(OK);
}

int ClientInterface::increment_file_index(Interface &socket) {
    verifyIdle(socket);
    if (impl()->getFileWriteEnable()) {
        LOG(logDEBUG1) << "Incrementing file index";
        impl()->setFileIndex(impl()->getFileIndex() + 1);
    }
    return socket.Send(OK);
}


int ClientInterface::set_additional_json_parameter(Interface &socket) {
    char args[2][SHORT_STR_LENGTH]{};
    socket.Receive(args);
    verifyIdle(socket);
    LOG(logDEBUG1) << "Setting additional json parameter (" << args[0] << "): " << args[1];
    impl()->setAdditionalJsonParameter(args[0], args[1]);
    return socket.Send(OK);
}

int ClientInterface::get_additional_json_parameter(Interface &socket) {
    char arg[SHORT_STR_LENGTH]{};
    socket.Receive(arg);
    char retval[SHORT_STR_LENGTH]{};
    sls::strcpy_safe(retval, impl()->getAdditionalJsonParameter(arg).c_str());
    LOG(logDEBUG1) << "additional json parameter (" << arg << "):" << retval;
    return socket.sendResult(retval);
}

int ClientInterface::get_progress(Interface &socket) {
    int retval = impl()->getProgress();
    LOG(logDEBUG1) << "progress retval: " << retval;
    return socket.sendResult(retval);
}