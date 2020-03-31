#include "slsDetectorFunctionList.h"
#include "RegisterDefs.h"
#include "versionAPI.h"
#include "clogger.h"
#include "DAC6571.h"
#include "LTC2620_Driver.h"
#include "common.h"
#include "ALTERA_PLL_CYCLONE10.h" 
#include "ASIC_Driver.h"
#ifdef VIRTUAL
#include "communication_funcs_UDP.h"
#endif

#include <string.h>
#include <unistd.h>     // usleep
#include <netinet/in.h>
#ifdef VIRTUAL
#include <pthread.h>
#include <time.h>
#endif


// Global variable from slsDetectorServer_funcs
extern int debugflag;
extern int checkModuleFlag;
extern udpStruct udpDetails;
extern const enum detectorType myDetectorType;

// Global variable from communication_funcs.c
extern void getMacAddressinString(char* cmac, int size, uint64_t mac);
extern void getIpAddressinString(char* cip, uint32_t ip);

int initError = OK;
int initCheckDone = 0;
char initErrorMessage[MAX_STR_LENGTH];

#ifdef VIRTUAL
pthread_t pthread_virtual_tid;
int virtual_status = 0;
int virtual_stop = 0;
#endif

enum detectorSettings thisSettings = UNINITIALIZED;
int32_t clkPhase[NUM_CLOCKS] = {};
uint32_t clkFrequency[NUM_CLOCKS] = {};
uint32_t systemFrequency = 0;
int highvoltage = 0;
int dacValues[NDAC] = {};
int onChipdacValues[ONCHIP_NDAC][NCHIP] = {};
int injectedChannelsOffset = 0;
int injectedChannelsIncrement = 0;
int vetoReference[NCHIP][NCHAN];
uint8_t adcConfiguration[NCHIP][NADC];
int burstMode = BURST_INTERNAL;
int64_t numTriggersReg = 1;
int64_t delayReg = 0;
int64_t numBurstsReg = 1;
int64_t burstPeriodReg = 0;
int detPos[2] = {};

int isInitCheckDone() {
	return initCheckDone;
}

int getInitResult(char** mess) {
	*mess = initErrorMessage;
	return initError;
}

void basictests() {
    initError = OK;
    initCheckDone = 0;
    memset(initErrorMessage, 0, MAX_STR_LENGTH);
#ifdef VIRTUAL
    LOG(logINFOBLUE, ("******** Gotthard2 Virtual Server *****************\n"));
    if (mapCSP0() == FAIL) {
    	strcpy(initErrorMessage,
				"Could not map to memory. Dangerous to continue.\n");
		LOG(logERROR, (initErrorMessage));
		initError = FAIL;
		return;
    }
    return;
#else
	LOG(logINFOBLUE, ("************ Gotthard2 Server *********************\n"));
	if (mapCSP0() == FAIL) {
    	strcpy(initErrorMessage,
				"Could not map to memory. Dangerous to continue.\n");
		LOG(logERROR, ("%s\n\n", initErrorMessage));
		initError = FAIL;
		return;
    }
	// does check only if flag is 0 (by default), set by command line
	if ((!debugflag) && ((checkType() == FAIL) || (testFpga() == FAIL) || (testBus() == FAIL))) {
		sprintf(initErrorMessage,
				"Could not pass basic tests of FPGA and bus. Dangerous to continue. (Firmware version:0x%llx) \n", getFirmwareVersion());
		LOG(logERROR, ("%s\n\n", initErrorMessage));
		initError = FAIL;
		return;
	}

	uint16_t hversion			= getHardwareVersionNumber();
	uint32_t ipadd				= getDetectorIP();
	uint64_t macadd				= getDetectorMAC();
	int64_t fwversion 			= getFirmwareVersion();
	int64_t swversion 			= getServerVersion();
	int64_t sw_fw_apiversion    = getFirmwareAPIVersion();
	int64_t client_sw_apiversion = getClientServerAPIVersion();
	uint32_t requiredFirmwareVersion = REQRD_FRMWRE_VRSN;

	LOG(logINFOBLUE, ("*************************************************\n"
			"Hardware Version:\t\t 0x%x\n"
			
			"Detector IP Addr:\t\t 0x%x\n"
			"Detector MAC Addr:\t\t 0x%llx\n\n"

			"Firmware Version:\t\t 0x%llx\n"
			"Software Version:\t\t 0x%llx\n"
			"F/w-S/w API Version:\t\t 0x%llx\n"
			"Required Firmware Version:\t 0x%x\n"
			"Client-Software API Version:\t 0x%llx\n"
			"********************************************************\n",
			hversion, 
			ipadd,
			(long  long unsigned int)macadd,
			(long  long int)fwversion,
			(long  long int)swversion,
			(long  long int)sw_fw_apiversion,
			requiredFirmwareVersion,
			(long long int)client_sw_apiversion
	));

	// return if flag is not zero, debug mode
	if (debugflag) {
		return;
	}

	//cant read versions
    LOG(logINFO, ("Testing Firmware-software compatibility:\n"));
	if(!fwversion || !sw_fw_apiversion){
		strcpy(initErrorMessage,
				"Cant read versions from FPGA. Please update firmware.\n");
		LOG(logERROR, (initErrorMessage));
		initError = FAIL;
		return;
	}

	//check for API compatibility - old server
	if(sw_fw_apiversion > requiredFirmwareVersion){
		sprintf(initErrorMessage,
				"This detector software software version (0x%llx) is incompatible.\n"
				"Please update detector software (min. 0x%llx) to be compatible with this firmware.\n",
				(long long int)sw_fw_apiversion,
				(long long int)requiredFirmwareVersion);
		LOG(logERROR, (initErrorMessage));
		initError = FAIL;
		return;
	}

	//check for firmware compatibility - old firmware
	if( requiredFirmwareVersion > fwversion) {
		sprintf(initErrorMessage,
				"This firmware version (0x%llx) is incompatible.\n"
				"Please update firmware (min. 0x%llx) to be compatible with this server.\n",
				(long long int)fwversion,
				(long long int)requiredFirmwareVersion);
		LOG(logERROR, (initErrorMessage));
		initError = FAIL;
		return;
	}
	LOG(logINFO, ("Compatibility - success\n"));
#endif
}

int checkType() {
#ifdef VIRTUAL
    return OK;
#endif
	u_int32_t type = ((bus_r(FPGA_VERSION_REG) & DETECTOR_TYPE_MSK) >> DETECTOR_TYPE_OFST);
	if (type != GOTTHARD2){
			LOG(logERROR, ("This is not a Gotthard2 firmware (read %d, expected %d)\n", type, GOTTHARD2));
			return FAIL;
		}
	return OK;
}

int testFpga() {
#ifdef VIRTUAL
    return OK;
#endif
	LOG(logINFO, ("Testing FPGA:\n"));

	//fixed pattern
	int ret = OK;
	volatile u_int32_t val = bus_r(FIX_PATT_REG);
	if (val == FIX_PATT_VAL) {
		LOG(logINFO, ("Fixed pattern: successful match 0x%08x\n",val));
	} else {
		LOG(logERROR, ("Fixed pattern does not match! Read 0x%08x, expected 0x%08x\n", val, FIX_PATT_VAL));
		ret = FAIL;
	}
	return ret;
}

int testBus() {
#ifdef VIRTUAL
    return OK;
#endif
	LOG(logINFO, ("Testing Bus:\n"));

	int ret = OK;
	u_int32_t addr = DTA_OFFSET_REG; 
	u_int32_t times = 1000 * 1000;
	u_int32_t i = 0;

	for (i = 0; i < times; ++i) {
		bus_w(addr, i * 100);
		if (i * 100 != bus_r(addr)) {
			LOG(logERROR, ("Mismatch! Wrote 0x%x, read 0x%x\n",
					i * 100, bus_r(addr)));
			ret = FAIL;
		}
	}

	bus_w(addr, 0);

	if (ret == OK) {
		LOG(logINFO, ("Successfully tested bus %d times\n", times));
	}
	return ret;
}

/* Ids */

uint64_t getServerVersion() {
    return APIGOTTHARD2;
}

uint64_t getClientServerAPIVersion() {
    return APIGOTTHARD2;
}

u_int64_t getFirmwareVersion() {
#ifdef VIRTUAL
    return 0;
#endif
	return ((bus_r(FPGA_VERSION_REG) & FPGA_COMPILATION_DATE_MSK) >> FPGA_COMPILATION_DATE_OFST);
}

u_int64_t getFirmwareAPIVersion() {
#ifdef VIRTUAL
    return 0;
#endif
    return ((bus_r(API_VERSION_REG) & API_VERSION_MSK) >> API_VERSION_OFST);
}

u_int16_t getHardwareVersionNumber() {
#ifdef VIRTUAL
    return 0;
#endif
	return ((bus_r(MCB_SERIAL_NO_REG) & MCB_SERIAL_NO_VRSN_MSK) >> MCB_SERIAL_NO_VRSN_OFST);
}

u_int32_t getDetectorNumber(){
#ifdef VIRTUAL
    return 0;
#endif
	return bus_r(MCB_SERIAL_NO_REG);
}


u_int64_t  getDetectorMAC() {
#ifdef VIRTUAL
    return 0;
#else
	char output[255],mac[255]="";
	u_int64_t res=0;
	FILE* sysFile = popen("ifconfig eth0 | grep HWaddr | cut -d \" \" -f 11", "r");
	fgets(output, sizeof(output), sysFile);
	pclose(sysFile);
	//getting rid of ":"
	char * pch;
	pch = strtok (output,":");
	while (pch != NULL){
		strcat(mac,pch);
		pch = strtok (NULL, ":");
	}
	sscanf(mac,"%llx",&res);
	return res;
#endif
}

u_int32_t  getDetectorIP(){
#ifdef VIRTUAL
    return 0;
#endif
	char temp[50]="";
	u_int32_t res=0;
	//execute and get address
	char output[255];
	FILE* sysFile = popen("ifconfig  | grep 'inet addr:'| grep -v '127.0.0.1' | cut -d: -f2", "r");
	fgets(output, sizeof(output), sysFile);
	pclose(sysFile);

	//converting IPaddress to hex.
	char* pcword = strtok (output,".");
	while (pcword != NULL) {
		sprintf(output,"%02x",atoi(pcword));
		strcat(temp,output);
		pcword = strtok (NULL, ".");
	}
	strcpy(output,temp);
	sscanf(output, "%x", 	&res);
	//LOG(logINFO, ("ip:%x\n",res);

	return res;
}


/* initialization */

void initControlServer(){
	CreateNotificationForCriticalTasks();
	if (initError == OK) {
		setupDetector();
	}
	initCheckDone = 1;
	if (initError == OK) {
		NotifyServerStartSuccess();
	}
}

void initStopServer() {

	usleep(CTRL_SRVR_INIT_TIME_US);
	if (mapCSP0() == FAIL) {
		LOG(logERROR, ("Stop Server: Map Fail. Dangerous to continue. Goodbye!\n"));
		exit(EXIT_FAILURE);
	}
}


/* set up detector */

void setupDetector() {
    LOG(logINFO, ("This Server is for 1 Gotthard2 module \n")); 

	clkFrequency[READOUT_C0] = DEFAULT_READOUT_C0;
	clkFrequency[READOUT_C1] = DEFAULT_READOUT_C1;
	clkFrequency[SYSTEM_C0] = DEFAULT_SYSTEM_C0;
	clkFrequency[SYSTEM_C1] = DEFAULT_SYSTEM_C1;
	clkFrequency[SYSTEM_C2] = DEFAULT_SYSTEM_C2;
	clkFrequency[SYSTEM_C3] = DEFAULT_SYSTEM_C3;
	systemFrequency = INT_SYSTEM_C0_FREQUENCY;
	detPos[0] = 0;
	detPos[1] = 0;

	thisSettings = UNINITIALIZED;
	highvoltage = 0;
	injectedChannelsOffset = 0;
	injectedChannelsIncrement = 0;
	burstMode = BURST_INTERNAL;
 	numTriggersReg = 1;
	delayReg = 0;
 	numBurstsReg = 1;
 	burstPeriodReg = 0;
	{
		int i, j;
		for (i = 0; i < NUM_CLOCKS; ++i) {
            clkPhase[i] = 0;
        }
		for (i = 0; i < NDAC; ++i) {
			dacValues[i] = 0;
		}
		for (i = 0; i < ONCHIP_NDAC; ++i) {
			for (j = 0; j < NCHIP; ++j) {
				onChipdacValues[i][j] = -1;
			}
		}
		for	(i = 0; i < NCHIP; ++i) {
			for (j = 0; j < NCHAN; ++j) {
				vetoReference[i][j] = 0;
			}
			for (j = 0; j < NADC; ++j) {
				adcConfiguration[i][j] = 0;
			}
		}	
	}


	// pll defines
	ALTERA_PLL_C10_SetDefines(REG_OFFSET, BASE_READOUT_PLL, BASE_SYSTEM_PLL, PLL_RESET_REG, PLL_RESET_REG, PLL_RESET_READOUT_MSK, PLL_RESET_SYSTEM_MSK, READOUT_PLL_VCO_FREQ_HZ, SYSTEM_PLL_VCO_FREQ_HZ);
	ALTERA_PLL_C10_ResetPLL(READOUT_PLL);
	ALTERA_PLL_C10_ResetPLL(SYSTEM_PLL);
	// hv
    DAC6571_SetDefines(HV_HARD_MAX_VOLTAGE, HV_DRIVER_FILE_NAME);
	// dacs
	LTC2620_D_SetDefines(DAC_MAX_MV, DAC_DRIVER_FILE_NAME, NDAC);
	// on chip dacs
	ASIC_Driver_SetDefines(ONCHIP_DAC_DRIVER_FILE_NAME);
	setTimingSource(DEFAULT_TIMING_SOURCE);

	// Default values
    setHighVoltage(DEFAULT_HIGH_VOLTAGE);

	// check module type attached if not in debug mode
	{
		int ret = checkDetectorType();
		if (checkModuleFlag) {
			switch (ret) {
				case -1:
					sprintf(initErrorMessage, "Could not get the module type attached.\n");
					initError = FAIL;		
					LOG(logERROR, ("Aborting startup!\n\n", initErrorMessage));
					return;
				case -2:
					sprintf(initErrorMessage, "No Module attached! Run server with -nomodule.\n");
					initError = FAIL;		
					LOG(logERROR, ("Aborting startup!\n\n", initErrorMessage));
					return;
				case FAIL:
					sprintf(initErrorMessage, "Wrong Module (Not Gotthard2) attached!\n");
					initError = FAIL;		
					LOG(logERROR, ("Aborting startup!\n\n", initErrorMessage));
					return;
				default:
					break;
			}
		} else {
			LOG(logINFOBLUE, ("In No-Module mode: Ignoring module type. Continuing.\n"));
		}
	}

	// power on chip
	powerChip(1);

#ifndef VIRTUAL
	// also sets default dac and on chip dac values 
	if (readConfigFile() == FAIL) {
		return;
	}
#endif
	setBurstMode(DEFAULT_BURST_MODE);
	setSettings(DEFAULT_SETTINGS);

	// Initialization of acquistion parameters
	setNumFrames(DEFAULT_NUM_FRAMES);
	setNumTriggers(DEFAULT_NUM_CYCLES);
	setNumBursts(DEFAULT_NUM_BURSTS);
	setExpTime(DEFAULT_EXPTIME);
	setPeriod(DEFAULT_PERIOD);
	setDelayAfterTrigger(DEFAULT_DELAY_AFTER_TRIGGER);
	setBurstPeriod(DEFAULT_BURST_PERIOD);
	setTiming(DEFAULT_TIMING_MODE);
	setCurrentSource(DEFAULT_CURRENT_SOURCE);
}

int readConfigFile() {

	if (initError == FAIL) {
		return initError;
	}

	// require a sleep before and after the rst dac signal
	usleep (INITIAL_STARTUP_WAIT);	

	// inform FPGA that onchip dacs will be configured soon
	LOG(logINFO, ("Setting configuration starting bit\n"));
	bus_w(ASIC_CONFIG_REG, bus_r(ASIC_CONFIG_REG) | ASIC_CONFIG_RST_DAC_MSK);

	usleep (INITIAL_STARTUP_WAIT);

    FILE* fd = fopen(CONFIG_FILE, "r");
    if(fd == NULL) {
		sprintf(initErrorMessage, "Could not open on-board detector server config file [%s].\n", CONFIG_FILE);
		initError = FAIL;		
		LOG(logERROR, ("%s\n\n", initErrorMessage));
        return FAIL;
    }

    LOG(logINFOBLUE, ("Reading config file %s\n", CONFIG_FILE));

    // Initialization
    const size_t LZ = 256;
    char line[LZ];
    memset(line, 0, LZ);
    char command[LZ];

	int nadcRead = 0;

    // keep reading a line
    while (fgets(line, LZ, fd)) {

		// ignore comments
        if (line[0] == '#') {
			LOG(logDEBUG1, ("Ignoring Comment\n"));
            continue;
		}

		// ignore empty lines
		if (strlen(line) <= 1) {
			LOG(logDEBUG1, ("Ignoring Empty line\n"));
			continue;
		}

		LOG(logDEBUG1, ("Command to process: (size:%d) %.*s\n", strlen(line), strlen(line) -1, line));
		memset(command, 0, LZ);

		// vetoref command
		if (!strncmp(line, "vetoref", strlen("vetoref"))) {
			int igain = 0;
			int value = 0;

			// cannot scan values
			if (sscanf(line, "%s %d 0x%x", command, &igain, &value) != 3) {
				sprintf(initErrorMessage, "Could not scan vetoref commands from on-board server config file. Line:[%s].\n", line);
				break;
			}	
			//validations
			if (igain < 0 || igain > 2) {
				sprintf(initErrorMessage, "Could not set veto reference from on-board server config file. Invalid gain index. Line:[%s].\n", line);
				break;				
			}
			//validations
			if (value > ADU_MAX_VAL) {
				sprintf(initErrorMessage, "Could not set veto reference from on-board server config file. Invalid value (max 0x%x). Line:[%s].\n", ADU_MAX_VAL, line);
				break;				
			}
			if (setVetoReference(igain, value) == FAIL) {
				sprintf(initErrorMessage, "Could not set veto reference from on-board server config file. Line:[%s].\n", line);
				break;					
			}
		}

		// confadc command
		else if (!strncmp(line, "confadc", strlen("confadc"))) {
			int ichip = -1;
			int iadc = -1;
			int value = 0;
			
			// cannot scan values
			if (sscanf(line, "%s %d %d 0x%x", command, &ichip, &iadc, &value) != 4) {
				sprintf(initErrorMessage, "Could not scan confadc commands from on-board server config file. Line:[%s].\n", line);
				break;
			}	
			//validations
			if (ichip < -1 ||ichip >= NCHIP) {
				sprintf(initErrorMessage, "Could not configure adc from on-board server config file. Invalid chip index. Line:[%s].\n", line);
				break;
			}
			if (iadc < -1 || iadc >= NADC) {
				sprintf(initErrorMessage, "Could not configure adc from on-board server config file. Invalid adc index. Line:[%s].\n", line);
				break;				
			}
			//validations
			if (value > ASIC_ADC_MAX_VAL) {
				sprintf(initErrorMessage, "Could not configure adc from on-board server config file. Invalid value (max 0x%x). Line:[%s].\n", ASIC_ADC_MAX_VAL, line);
				break;				
			}

			int chipmin = 0;
			int chipmax = NCHIP;
			int adcmin = 0;
			int adcmax = NADC;

			// specific chip
			if (ichip != -1) {
				chipmin = ichip;
				chipmax = ichip + 1;
			}
			// specific adc
			if (iadc != -1) {
				adcmin = iadc;
				adcmax = iadc + 1;
			}

			int i, j;
			for (i = chipmin; i < chipmax; ++i) {
				for (j = adcmin; j < adcmax; ++j) {
					adcConfiguration[i][j] = (uint8_t)value;
					++nadcRead;
				}
			}
		}


        // vchip command
        else if (!strncmp(line, "vchip_", strlen("vchip_"))) {

			enum ONCHIP_DACINDEX idac = 0;
			int ichip = -1;
			int value = 0;

			// cannot scan values
			if (sscanf(line, "%s %d 0x%x", command, &ichip, &value) != 3) {
				sprintf(initErrorMessage, "Could not scan on-chip dac commands from on-board server config file. Line:[%s].\n", line);
				break;
			}

            if  (!strcmp(command,"vchip_comp_fe")) {
                idac = G2_VCHIP_COMP_FE;
            } else if (!strcasecmp(command,"vchip_opa_1st")) {
                idac = G2_VCHIP_OPA_1ST;
            } else if (!strcasecmp(command,"vchip_opa_fd")) {
                idac = G2_VCHIP_OPA_FD;
            } else if (!strcasecmp(command,"vchip_comp_adc")) {
                idac = G2_VCHIP_COMP_ADC;
            } else if (!strcasecmp(command,"vchip_ref_comp_fe")) {
                idac = G2_VCHIP_REF_COMP_FE;
            } else if (!strcasecmp(command,"vchip_cs")) {
                idac = G2_VCHIP_CS;
            } else {
				sprintf(initErrorMessage, "Unknown on-chip dac command in on-board server config file. Command:[%s].\n", command);
                break;
            }

			// set on chip dac
			if (setOnChipDAC(idac, ichip, value) == FAIL) {
				sprintf(initErrorMessage, "Set on-chip dac failed from on-board server config file. Command:[%s].\n", command);
                break;				
			}
        }

        // dac command
        else {

			enum DACINDEX idac = 0;
			int value = 0;

			// cannot scan values
			if (sscanf(line, "%s %d", command, &value) != 2) {
				sprintf(initErrorMessage, "Could not scan dac commands from on-board server config file. Line:[%s].\n", line);
				break;
			}

            if  (!strcmp(command,"vref_h_adc")) {
                idac = G2_VREF_H_ADC;
            } else if (!strcasecmp(command,"vb_comp_fe")) {
                idac = G2_VB_COMP_FE;
            } else if (!strcasecmp(command,"vb_comp_adc")) {
                idac = G2_VB_COMP_ADC;
            } else if (!strcasecmp(command,"vcom_cds")) {
                idac = G2_VCOM_CDS;
            } else if (!strcasecmp(command,"vref_rstore")) {
                idac = G2_VREF_RSTORE;
            } else if (!strcasecmp(command,"vb_opa_1st")) {
                idac = G2_VB_OPA_1ST;
            } else if (!strcasecmp(command,"vref_comp_fe")) {
                idac = G2_VREF_COMP_FE;
            } else if (!strcasecmp(command,"vcom_adc1")) {
                idac = G2_VCOM_ADC1;
            } else if (!strcasecmp(command,"vref_prech")) {
                idac = G2_VREF_PRECH;
            } else if (!strcasecmp(command,"vref_l_adc")) {
                idac = G2_VREF_L_ADC;
            } else if (!strcasecmp(command,"vref_cds")) {
                idac = G2_VREF_CDS;
            } else if (!strcasecmp(command,"vb_cs")) {
                idac = G2_VB_CS;
            } else if (!strcasecmp(command,"vb_opa_fd")) {
                idac = G2_VB_OPA_FD;
            } else if (!strcasecmp(command,"vcom_adc2")) {
                idac = G2_VCOM_ADC2;
            } else {
				sprintf(initErrorMessage, "Unknown command in on-board server config file. Command:[%s].\n", command);
                break;
            }

			// set dac
			setDAC(idac, value, 0);
			int retval = getDAC(idac, 0);
			if (retval != value) {
				sprintf(initErrorMessage, "Set dac %s failed from on-board server config file. Set %d, got %d.\n", command, value, retval);
                break;				
			}
        }
		memset(line, 0, LZ);
    }
    fclose(fd);

	if (!strlen(initErrorMessage)) {
		if (nadcRead != NADC * NCHIP) {
			sprintf(initErrorMessage, "Could not configure adc from on-board server config file. Insufficient adcconf commands. Read %d, expected %d\n", nadcRead, NADC * NCHIP);				
		}
	}
	{
		int i = 0, j = 0;
		for (i = 0; i < NCHIP; ++i) {
			for (j = 0; j < NADC; ++j) {
				LOG(logDEBUG2, ("adc read %d %d: 0x%02hhx\n", i, j, adcConfiguration[i][j]));
			}
		}
	}

	if (strlen(initErrorMessage)) {
		initError = FAIL;		
		LOG(logERROR, ("%s\n\n", initErrorMessage));
	} else {
		LOG(logINFOBLUE, ("Successfully read config file\n"));

		// inform FPGA that onchip dacs will be configured soon
		LOG(logINFO, ("Setting configuration done bit\n"));
		bus_w(ASIC_CONFIG_REG, bus_r(ASIC_CONFIG_REG) | ASIC_CONFIG_DONE_MSK);
	}
    return initError;
}

/* firmware functions (resets) */

void cleanFifos() {
#ifdef VIRTUAL
    return;
#endif
	LOG(logINFO, ("Clearing Acquisition Fifos\n"));
	bus_w(CONTROL_REG, bus_r(CONTROL_REG) | CONTROL_CLR_ACQSTN_FIFO_MSK);
}

void resetCore() {
#ifdef VIRTUAL
    return;
#endif
	LOG(logINFO, ("Resetting Core\n"));
	bus_w(CONTROL_REG, bus_r(CONTROL_REG) | CONTROL_CRE_RST_MSK);
}

void resetPeripheral() {
#ifdef VIRTUAL
    return;
#endif
	LOG(logINFO, ("Resetting Peripheral\n"));
	bus_w(CONTROL_REG, bus_r(CONTROL_REG) | CONTROL_PRPHRL_RST_MSK);
}

/* set parameters -  dr, roi */

int setDynamicRange(int dr){
	return DYNAMIC_RANGE;
}


/* parameters - timer */
void setNumFrames(int64_t val) {
    if (val > 0) {
		if (burstMode == BURST_OFF) {
    		LOG(logINFO, ("Setting number of frames %lld [Continuous mode]\n", val));
			set64BitReg(val, SET_FRAMES_LSB_REG, SET_FRAMES_MSB_REG);
		} else {
    		LOG(logINFO, ("Setting number of frames %d [Burst mode]\n", (int)val));
			bus_w(ASIC_INT_FRAMES_REG, bus_r(ASIC_INT_FRAMES_REG) &~ ASIC_INT_FRAMES_MSK);
			bus_w(ASIC_INT_FRAMES_REG, bus_r(ASIC_INT_FRAMES_REG) | (((int)val << ASIC_INT_FRAMES_OFST) & ASIC_INT_FRAMES_MSK));
		}
    }
}

int64_t getNumFrames() {
	if (burstMode == BURST_OFF) {
		return get64BitReg(SET_FRAMES_LSB_REG, SET_FRAMES_MSB_REG);
	} else {
		return ((bus_r(ASIC_INT_FRAMES_REG) & ASIC_INT_FRAMES_MSK) >> ASIC_INT_FRAMES_OFST);
	}	
}

void setNumTriggers(int64_t val) {
    if (val > 0) {
		LOG(logINFO, ("Setting number of triggers %lld\n", val));
		if (getTiming() == AUTO_TIMING) {
			LOG(logINFO, ("\tNot trigger mode: not writing to register\n"));
			numTriggersReg = val;
		} else {
        	set64BitReg(val, SET_CYCLES_LSB_REG, SET_CYCLES_MSB_REG);
		}
    } 
}

int64_t getNumTriggers() {
	if (getTiming() == AUTO_TIMING) {
		return numTriggersReg;
	}
	return get64BitReg(SET_CYCLES_LSB_REG, SET_CYCLES_MSB_REG);
}

void setNumBursts(int64_t val) {
    if (val > 0) {
		LOG(logINFO, ("Setting number of bursts %lld\n", val));
		if (burstMode != BURST_OFF && getTiming() == AUTO_TIMING) {
			set64BitReg(val, SET_FRAMES_LSB_REG, SET_FRAMES_MSB_REG);
		} else {
        	LOG(logINFO, ("\tNot (Burst and Auto mode): not writing to register\n"));
			numBurstsReg = val;
		}
    } 
}

int64_t getNumBursts() {
	if (burstMode != BURST_OFF && getTiming() == AUTO_TIMING) {
		return get64BitReg(SET_FRAMES_LSB_REG, SET_FRAMES_MSB_REG);
	}
	return numBurstsReg;
}

int setExpTime(int64_t val) {
    if (val < 0) {
        LOG(logERROR, ("Invalid exptime: %lld ns\n", val));
        return FAIL;
    }
	LOG(logINFO, ("Setting exptime %lld ns\n", val));
	val *= (1E-9 * systemFrequency);
    set64BitReg(val, ASIC_INT_EXPTIME_LSB_REG, ASIC_INT_EXPTIME_MSB_REG);

    // validate for tolerance
	int64_t retval = getExpTime();
    val /= (1E-9 * systemFrequency);
    if (val != retval) {
        return FAIL;
    }
    return OK;
}

int64_t getExpTime() {
	return get64BitReg(ASIC_INT_EXPTIME_LSB_REG, ASIC_INT_EXPTIME_MSB_REG) / (1E-9 * systemFrequency);
}

int setPeriod(int64_t val) {
    if (val < 0) {
        LOG(logERROR, ("Invalid period: %lld ns\n", val));
        return FAIL;
    }
    val *= (1E-9 * systemFrequency);
	if (burstMode == BURST_OFF) {
		LOG(logINFO, ("Setting period %lld ns [Continuous mode]\n", val));
    	set64BitReg(val, SET_PERIOD_LSB_REG, SET_PERIOD_MSB_REG);
	} else {
		LOG(logINFO, ("Setting period %lld ns [Burst mode]\n", val));
    	set64BitReg(val, ASIC_INT_PERIOD_LSB_REG, ASIC_INT_PERIOD_MSB_REG);
	}
	// validate for tolerance
    int64_t retval = getPeriod();
    val /= (1E-9 * systemFrequency);
    if (val != retval) {
        return FAIL;
    }
    return OK;
}

int64_t getPeriod() {
	if (burstMode == BURST_OFF) {
		return get64BitReg(SET_PERIOD_LSB_REG, SET_PERIOD_MSB_REG)/ (1E-9 * systemFrequency);
	} else {
		return get64BitReg(ASIC_INT_PERIOD_LSB_REG, ASIC_INT_PERIOD_MSB_REG)/ (1E-9 * systemFrequency);
	}
}

int setDelayAfterTrigger(int64_t val) {
    if (val < 0) {
        LOG(logERROR, ("Invalid delay after trigger: %lld ns\n", val));
        return FAIL;
    } 
	LOG(logINFO, ("Setting delay after trigger %lld ns\n", val));
	val *= (1E-9 * systemFrequency);
	if (getTiming() == AUTO_TIMING) {
		LOG(logINFO, ("\tNot trigger mode: not writing to register\n"));
		delayReg = val;
	} else {
   		set64BitReg(val, SET_TRIGGER_DELAY_LSB_REG, SET_TRIGGER_DELAY_MSB_REG);
	}
    // validate for tolerance
    int64_t retval = getDelayAfterTrigger();
    val /= (1E-9 * systemFrequency);
    if (val != retval) {
        return FAIL;
    }
    return OK;
}

int64_t getDelayAfterTrigger() {
	if (getTiming() == AUTO_TIMING) {
		return delayReg / (1E-9 * systemFrequency);
	}
    return get64BitReg(SET_TRIGGER_DELAY_LSB_REG, SET_TRIGGER_DELAY_MSB_REG) / (1E-9 * systemFrequency);
}

int setBurstPeriod(int64_t val) {
    if (val < 0) {
        LOG(logERROR, ("Invalid burst period: %lld ns\n", val));
        return FAIL;
    } 
	LOG(logINFO, ("Setting burst period %lld ns\n", val));
	val *= (1E-9 * systemFrequency);
	if (burstMode != BURST_OFF && getTiming() == AUTO_TIMING) {
    	set64BitReg(val, SET_PERIOD_LSB_REG, SET_PERIOD_MSB_REG);
	} else {
        LOG(logINFO, ("\tNot (Burst and Auto mode): not writing to register\n"));
		burstPeriodReg = val;
	}

    // validate for tolerance
    int64_t retval = getBurstPeriod();
    val /= (1E-9 * systemFrequency);
    if (val != retval) {
        return FAIL;
    }
    return OK;
}

int64_t getBurstPeriod() {
	if (burstMode != BURST_OFF && getTiming() == AUTO_TIMING) {
    	return get64BitReg(SET_PERIOD_LSB_REG, SET_PERIOD_MSB_REG) / (1E-9 * systemFrequency);
	}
	return burstPeriodReg / (1E-9 * systemFrequency);
}

int64_t getNumFramesLeft() {
    return get64BitReg(GET_FRAMES_LSB_REG, GET_FRAMES_MSB_REG);
}

int64_t getNumTriggersLeft() {
    return get64BitReg(GET_CYCLES_LSB_REG, GET_CYCLES_MSB_REG);
}

int64_t getDelayAfterTriggerLeft() {
    return get64BitReg(GET_DELAY_LSB_REG, GET_DELAY_MSB_REG) / (1E-9 * systemFrequency);
}

int64_t getPeriodLeft() {
    return get64BitReg(GET_PERIOD_LSB_REG, GET_PERIOD_MSB_REG) / (1E-9 * systemFrequency);
}

int64_t getFramesFromStart() {
    return get64BitReg(FRAMES_FROM_START_LSB_REG, FRAMES_FROM_START_MSB_REG);
}

int64_t getActualTime() {
    return get64BitReg(TIME_FROM_START_LSB_REG, TIME_FROM_START_MSB_REG) / (1E-9 * FIXED_PLL_FREQUENCY * 2);
}

int64_t getMeasurementTime() {
    return get64BitReg(START_FRAME_TIME_LSB_REG, START_FRAME_TIME_MSB_REG) / (1E-9 * FIXED_PLL_FREQUENCY);
}


/* parameters - module, settings */
enum detectorSettings setSettings(enum detectorSettings sett){
	if(sett == UNINITIALIZED)
		return thisSettings;

	// set settings
	uint32_t addr = ASIC_CONFIG_REG;
	uint32_t mask = ASIC_CONFIG_GAIN_MSK;
	if(sett != GET_SETTINGS) {
	    switch (sett) {
	    case DYNAMICGAIN:
	        bus_w(addr, bus_r(addr) & ~mask);
            bus_w(addr, bus_r(addr) | ASIC_CONFIG_DYNAMIC_GAIN_VAL);
            LOG(logINFO, ("Set settings - Dyanmic Gain, val: 0x%x\n", bus_r(addr) & mask));
	        break;
	    case FIXGAIN1:
	        bus_w(addr, bus_r(addr) & ~mask);
            bus_w(addr, bus_r(addr) | ASIC_CONFIG_FIX_GAIN_1_VAL);
            LOG(logINFO, ("Set settings - Fix Gain 1, val: 0x%x\n", bus_r(addr) & mask));
	        break;
	    case FIXGAIN2:
	        bus_w(addr, bus_r(addr) & ~mask);
            bus_w(addr, bus_r(addr) | ASIC_CONFIG_FIX_GAIN_2_VAL);
            LOG(logINFO, ("Set settings - Fix Gain 2, val: 0x%x\n", bus_r(addr) & mask));
	        break;
	    default:
	        LOG(logERROR, ("This settings is not defined for this detector %d\n", (int)sett));
	        return -1;
	    }
		thisSettings = sett;
	}

	return getSettings();
}


enum detectorSettings getSettings(){
	uint32_t regval = bus_r(ASIC_CONFIG_REG);
	uint32_t val = regval & ASIC_CONFIG_GAIN_MSK;
	LOG(logDEBUG1, ("Getting Settings\n Reading val :0x%x\n", val));

	switch(val) {
	case ASIC_CONFIG_RESERVED_VAL:
	case ASIC_CONFIG_DYNAMIC_GAIN_VAL:
        thisSettings = DYNAMICGAIN;
        LOG(logDEBUG1, ("Settings read: Dynamic Gain. val: 0x%x\n", val));
        break;
	case ASIC_CONFIG_FIX_GAIN_1_VAL:
        thisSettings = FIXGAIN1;
        LOG(logDEBUG1, ("Settings read: Fix Gain 1. val: 0x%x\n", val));
        break;
	case ASIC_CONFIG_FIX_GAIN_2_VAL:
        thisSettings = FIXGAIN2;
        LOG(logDEBUG1, ("Settings read: Fix Gain 2. val: 0x%x\n", val));
        break;
    default:
        thisSettings = UNDEFINED;
        LOG(logERROR, ("Settings read: Undefined. val: 0x%x\n", val));
	}
	return thisSettings;
}


/* parameters - dac, hv */
int	setOnChipDAC(enum ONCHIP_DACINDEX ind, int chipIndex, int val) {
	char* names[] = {ONCHIP_DAC_NAMES};
	LOG(logDEBUG1, ("Setting on chip dac[%d - %s]: 0x%x\n", (int)ind, names[ind], val));

	if (ind >= ONCHIP_NDAC) {
		LOG(logERROR, ("Invalid dac index %d\n", (int)ind));
		return FAIL;
	}
	if (chipIndex >= NCHIP) {
		LOG(logERROR, ("Invalid chip index %d\n", chipIndex));
		return FAIL;		
	}
	if (val > ONCHIP_DAC_MAX_VAL) {
		LOG(logERROR, ("Invalid val %d\n", val));
		return FAIL;			
	}
	LOG(logINFO, ("Setting on chip dac[%d - %s]: 0x%x\n", (int)ind, names[ind], val));

	char buffer[2];
	memset(buffer, 0, sizeof(buffer));
	buffer[1] = ((val & 0xF) << 4) | (((int)ind) & 0xF); // LSB (4 bits) + ADDR (4 bits)
	buffer[0] = (val >> 4) & 0x3F; // MSB (6 bits)
		
	if (ASIC_Driver_Set(chipIndex, sizeof(buffer), buffer) == FAIL) {
		return FAIL;				
	}
	// all chips
	if (chipIndex == -1) {
		int ichip = 0;
		for (ichip = 0; ichip < NCHIP; ++ichip) {
			onChipdacValues[ind][ichip] = val;
		}
	} 
	
	// specific chip
	else {
		onChipdacValues[ind][chipIndex] = val;
	}
	return OK;
}

int	getOnChipDAC(enum ONCHIP_DACINDEX ind, int chipIndex) {
	// all chips
	if (chipIndex == -1) {
		int retval = onChipdacValues[ind][0];
		int ichip = 0;
		// check if same value for remaining chips
		for (ichip = 1; ichip < NCHIP; ++ichip) {
			if (onChipdacValues[ind][ichip] != retval) {
				return -1;
			}
		}
		return retval;
	}
	// specific chip
	return onChipdacValues[ind][chipIndex];
}

void setDAC(enum DACINDEX ind, int val, int mV) {
    if (val < 0) {
        return;
	}

	char* dac_names[] = {DAC_NAMES};
    LOG(logDEBUG1, ("Setting dac[%d - %s]: %d %s \n", (int)ind, dac_names[ind], val, (mV ? "mV" : "dac units")));
    int dacval = val;
#ifdef VIRTUAL
    LOG(logINFO, ("Setting dac[%d - %s]: %d %s \n", (int)ind, dac_names[ind], val, (mV ? "mV" : "dac units")));
    if (!mV) {
        dacValues[ind] = val;
    }
    // convert to dac units
    else if (LTC2620_D_VoltageToDac(val, &dacval) == OK) {
        dacValues[ind] = dacval;
    }
#else
    if (LTC2620_D_SetDACValue((int)ind, val, mV, dac_names[ind], &dacval) == OK) {
        dacValues[ind] = dacval;
    }
#endif
}

int getDAC(enum DACINDEX ind, int mV) {
    if (!mV) {
        LOG(logDEBUG1, ("Getting DAC %d : %d dac\n",ind, dacValues[ind]));
        return dacValues[ind];
    }
    int voltage = -1;
    LTC2620_D_DacToVoltage(dacValues[ind], &voltage);
    LOG(logDEBUG1, ("Getting DAC %d : %d dac (%d mV)\n",ind, dacValues[ind], voltage));
    return voltage;
}

int getMaxDacSteps() {
    return LTC2620_D_GetMaxNumSteps();
}

int setHighVoltage(int val){
	if (val > HV_SOFT_MAX_VOLTAGE) {
		val = HV_SOFT_MAX_VOLTAGE;
	}
	
#ifdef VIRTUAL
    if (val >= 0)
        highvoltage = val;
    return highvoltage;
#endif

	// setting hv
	if (val >= 0) {
	    LOG(logINFO, ("Setting High voltage: %d V\n", val));
	    DAC6571_Set(val);
	    highvoltage = val;
	}
	return highvoltage;
}

/* parameters - timing */
void setTiming( enum timingMode arg){
	// update
	// trigger
	if (getTiming() == TRIGGER_EXPOSURE) {
		numTriggersReg = get64BitReg(SET_CYCLES_LSB_REG, SET_CYCLES_MSB_REG);
		delayReg = get64BitReg(SET_TRIGGER_DELAY_LSB_REG, SET_TRIGGER_DELAY_MSB_REG);
	}
	// auto and burst
	else if (burstMode != BURST_OFF) {
		numBurstsReg = get64BitReg(SET_FRAMES_LSB_REG, SET_FRAMES_MSB_REG);
		burstPeriodReg = get64BitReg(SET_PERIOD_LSB_REG, SET_PERIOD_MSB_REG);
	}

    switch(arg){
    case AUTO_TIMING:
        LOG(logINFO, ("Set Timing: Auto\n"));
        bus_w(EXT_SIGNAL_REG, bus_r(EXT_SIGNAL_REG) & ~EXT_SIGNAL_MSK);
        break;
    case TRIGGER_EXPOSURE:
        LOG(logINFO, ("Set Timing: Trigger\n"));
        bus_w(EXT_SIGNAL_REG, bus_r(EXT_SIGNAL_REG) | EXT_SIGNAL_MSK);
        break;
    default:
        LOG(logERROR, ("Unknown timing mode %d\n", arg));
    }

	LOG(logINFO, ("\tUpdating registers\n"))
	// trigger
	if (getTiming() == TRIGGER_EXPOSURE) {
		set64BitReg(numTriggersReg, SET_CYCLES_LSB_REG, SET_CYCLES_MSB_REG);
		set64BitReg(delayReg, SET_TRIGGER_DELAY_LSB_REG, SET_TRIGGER_DELAY_MSB_REG);
		LOG(logINFO, ("\tTriggers reg: %lld, Delay reg: %lldns\n", getNumTriggers(), getDelayAfterTrigger()));
		// burst
		if (burstMode != BURST_OFF) {
			LOG(logINFO, ("\tFrame reg: 1, Period reg: 0\n"))
			set64BitReg(1, SET_FRAMES_LSB_REG, SET_FRAMES_MSB_REG);
    		set64BitReg(0, SET_PERIOD_LSB_REG, SET_PERIOD_MSB_REG);
		}
	} 
	// auto
	else {
		LOG(logINFO, ("\tTrigger reg: 1, Delay reg: 0\n"))
        set64BitReg(1, SET_CYCLES_LSB_REG, SET_CYCLES_MSB_REG);
   		set64BitReg(0, SET_TRIGGER_DELAY_LSB_REG, SET_TRIGGER_DELAY_MSB_REG);
		// burst 
		if (burstMode != BURST_OFF) {
			set64BitReg(numBurstsReg, SET_FRAMES_LSB_REG, SET_FRAMES_MSB_REG);
    		set64BitReg(burstPeriodReg, SET_PERIOD_LSB_REG, SET_PERIOD_MSB_REG);
			LOG(logINFO, ("\tFrames reg (bursts): %lld, Period reg(burst period): %lldns\n", getNumBursts(), getBurstPeriod()));
		}
	}
	LOG(logINFO, ("\tDone Updating registers\n"))
}

enum timingMode getTiming() {
    if (bus_r(EXT_SIGNAL_REG) == EXT_SIGNAL_MSK)
        return TRIGGER_EXPOSURE;
    return AUTO_TIMING;
}


int configureMAC() {

	uint32_t srcip = udpDetails.srcip;
	uint32_t dstip = udpDetails.dstip;
	uint64_t srcmac = udpDetails.srcmac;
	uint64_t dstmac = udpDetails.dstmac;
	int srcport = udpDetails.srcport;
	int dstport = udpDetails.dstport;		

	LOG(logINFOBLUE, ("Configuring MAC\n"));
	char src_mac[50], src_ip[INET_ADDRSTRLEN],dst_mac[50], dst_ip[INET_ADDRSTRLEN];
	getMacAddressinString(src_mac, 50, srcmac);
	getMacAddressinString(dst_mac, 50, dstmac);
	getIpAddressinString(src_ip, srcip);
	getIpAddressinString(dst_ip, dstip);

	LOG(logINFO, (
	        "\tSource IP   : %s\n"
	        "\tSource MAC  : %s\n"
	        "\tSource Port : %d\n"
	        "\tDest IP     : %s\n"
	        "\tDest MAC    : %s\n"
			"\tDest Port   : %d\n",
	        src_ip, src_mac, srcport,
	        dst_ip, dst_mac, dstport));

#ifdef VIRTUAL
	if (setUDPDestinationDetails(0, dst_ip, dstport) == FAIL) {
		LOG(logERROR, ("could not set udp destination IP and port\n"));
		return FAIL;
	}
    return OK;
#endif

	// start addr
	uint32_t addr = BASE_UDP_RAM;
	// calculate rxr endpoint offset
	//addr += (iRxEntry * RXR_ENDPOINT_OFST);//TODO: is there round robin already implemented?
	// get struct memory
	udp_header *udp = (udp_header*) (Nios_getBaseAddress() + addr/(sizeof(u_int32_t)));
	memset(udp, 0, sizeof(udp_header));

	//  mac addresses	
	// msb (32) + lsb (16)
	udp->udp_destmac_msb	= ((dstmac >> 16) & BIT32_MASK);
	udp->udp_destmac_lsb	= ((dstmac >> 0) & BIT16_MASK);
	// msb (16) + lsb (32)
	udp->udp_srcmac_msb		= ((srcmac >> 32) & BIT16_MASK);
	udp->udp_srcmac_lsb		= ((srcmac >> 0) & BIT32_MASK);

	// ip addresses
	udp->ip_srcip_msb		= ((srcip >> 16) & BIT16_MASK);
	udp->ip_srcip_lsb		= ((srcip >> 0) & BIT16_MASK);	
	udp->ip_destip_msb		= ((dstip >> 16) & BIT16_MASK);
	udp->ip_destip_lsb		= ((dstip >> 0) & BIT16_MASK);	

	// source port
	udp->udp_srcport 		= srcport;
	udp->udp_destport		= dstport;

	// other defines
	udp->udp_ethertype		= 0x800;
	udp->ip_ver				= 0x4;
	udp->ip_ihl				= 0x5;
	udp->ip_flags			= 0x2; //FIXME
	udp->ip_ttl           	= 0x40;
	udp->ip_protocol      	= 0x11;
	// total length is redefined in firmware

	calcChecksum(udp);

	//TODO?
	cleanFifos();
	resetCore();
	//alignDeserializer();
	return OK;
}

void calcChecksum(udp_header* udp) {
	int count = IP_HEADER_SIZE;
	long int sum = 0;
	
	// start at ip_tos as the memory is not continous for ip header
	uint16_t *addr = (uint16_t*) (&(udp->ip_tos)); 

	sum += *addr++;
	count -= 2;

	// ignore ethertype (from udp header)
	addr++;

	// from identification to srcip_lsb
    while( count > 2 )  {
		sum += *addr++;
		count -= 2;
	}

	// ignore src udp port (from udp header)
	addr++;
	
	if (count > 0)
	    sum += *addr;                     // Add left-over byte, if any
	while (sum >> 16)
	    sum = (sum & 0xffff) + (sum >> 16);// Fold 32-bit sum to 16 bits
	long int checksum = sum & 0xffff;
	checksum += UDP_IP_HEADER_LENGTH_BYTES;
	LOG(logINFO, ("\tIP checksum is 0x%lx\n",checksum));
	udp->ip_checksum = checksum;
}

int setDetectorPosition(int pos[]) {
    memcpy(detPos, pos, sizeof(detPos));

	uint32_t addr = COORD_0_REG;
	int value = 0;
	int valueRead = 0;
	int ret = OK;

	// row
	value = detPos[X];
	bus_w(addr, (bus_r(addr) &~COORD_ROW_MSK) | ((value << COORD_ROW_OFST) & COORD_ROW_MSK));
	valueRead = ((bus_r(addr) &  COORD_ROW_MSK) >> COORD_ROW_OFST);
	if (valueRead != value) {
		LOG(logERROR, ("Could not set row. Set %d, read %d\n", value, valueRead));
		ret = FAIL;
	}

	// col
	value = detPos[Y];
	bus_w(addr, (bus_r(addr) &~COORD_COL_MSK) | ((value << COORD_COL_OFST) & COORD_COL_MSK));
	valueRead = ((bus_r(addr) &  COORD_COL_MSK) >> COORD_COL_OFST);
	if (valueRead != value) {
		LOG(logERROR, ("Could not set column. Set %d, read %d\n", value, valueRead));
		ret = FAIL;
	}

	if (ret == OK) {
		LOG(logINFO, ("\tPosition set to [%d, %d]\n", detPos[X], detPos[Y]));
	} 
	
	return ret;
}

int* getDetectorPosition() {
    return detPos;
}

// Detector Specific

int checkDetectorType() {
#ifdef VIRTUAL	
	return OK;
#endif
	LOG(logINFO, ("Checking type of module\n"));
	FILE* fd = fopen(TYPE_FILE_NAME, "r");
    if (fd == NULL) {
        LOG(logERROR, ("Could not open file %s to get type of the module attached\n", TYPE_FILE_NAME));
        return -1;
    }	
	char buffer[MAX_STR_LENGTH];
	memset(buffer, 0, sizeof(buffer));
	fread (buffer, MAX_STR_LENGTH, sizeof(char), fd);
	if (strlen(buffer) == 0) {
        LOG(logERROR, ("Could not read file %s to get type of the module attached\n", TYPE_FILE_NAME));
        return -1;		
	}
	int type = atoi(buffer);
	if (type > TYPE_NO_MODULE_STARTING_VAL) {
        LOG(logERROR, ("No Module attached! Expected %d for Gotthard2, got %d\n", TYPE_GOTTHARD2_MODULE_VAL, type));
        return -2;	
	}

	if (abs(type - TYPE_GOTTHARD2_MODULE_VAL) > TYPE_TOLERANCE) {
        LOG(logERROR, ("Wrong Module attached! Expected %d for Gotthard2, got %d\n", TYPE_GOTTHARD2_MODULE_VAL, type));
        return FAIL;	
	}
	return OK;
}

int powerChip (int on){
    if(on != -1){
        if(on){
            LOG(logINFO, ("Powering chip: on\n"));
            bus_w(CONTROL_REG, bus_r(CONTROL_REG) | CONTROL_PWR_CHIP_MSK);
        }
        else{
            LOG(logINFO, ("Powering chip: off\n"));
            bus_w(CONTROL_REG, bus_r(CONTROL_REG) & ~CONTROL_PWR_CHIP_MSK);
        }
    }
    return ((bus_r(CONTROL_REG) & CONTROL_PWR_CHIP_MSK) >> CONTROL_PWR_CHIP_OFST);
}

int setPhase(enum CLKINDEX ind, int val, int degrees) {
   if (ind < 0 || ind >= NUM_CLOCKS) {
		LOG(logERROR, ("Unknown clock index %d to set phase\n", ind));
	    return FAIL;
	}
	char* clock_names[] = {CLK_NAMES};
    LOG(logINFOBLUE, ("Setting %s clock (%d) phase to %d %s\n", clock_names[ind], ind, val, degrees == 0 ? "" : "degrees"));
	int maxShift = getMaxPhase(ind);
	// validation
	if (degrees && (val < 0 || val > 359)) {
		 LOG(logERROR, ("\tPhase outside limits (0 - 359°C)\n"));
		 return FAIL;
	}
	if (!degrees && (val < 0 || val > maxShift - 1)) {
		 LOG(logERROR, ("\tPhase outside limits (0 - %d phase shifts)\n", maxShift - 1));
		 return FAIL;
	}

	int valShift = val;
	// convert to phase shift
	if (degrees) {
		ConvertToDifferentRange(0, 359, 0, maxShift - 1, val, &valShift);
	}
	LOG(logDEBUG1, ("\tphase shift: %d (degrees/shift: %d)\n", valShift, val));

	int relativePhase = valShift - clkPhase[ind];
	LOG(logDEBUG1, ("\trelative phase shift: %d (Current phase: %d)\n", relativePhase, clkPhase[ind]));

    // same phase
    if (!relativePhase) {
    	LOG(logINFO, ("\tNothing to do in Phase Shift\n"));
    	return OK;
    }

	int direction = 1;
	if (relativePhase < 0) {
		relativePhase *= -1;
		direction = 0;
	}
	int pllIndex = (int)(ind >= SYSTEM_C0 ? SYSTEM_PLL : READOUT_PLL);
	int clkIndex = (int)(ind >= SYSTEM_C0 ? ind - SYSTEM_C0 : ind);
    ALTERA_PLL_C10_SetPhaseShift(pllIndex, clkIndex, relativePhase, direction);

    clkPhase[ind] = valShift;
	return OK;
}

int getPhase(enum CLKINDEX ind, int degrees) {
   if (ind < 0 || ind >= NUM_CLOCKS) {
		LOG(logERROR, ("Unknown clock index %d to get phase\n", ind));
	    return -1;
	}
	if (!degrees)
		return clkPhase[ind];
	// convert back to degrees
	int val = 0;
	ConvertToDifferentRange(0, getMaxPhase(ind) - 1, 0, 359, clkPhase[ind], &val);
	return val;
}

int getMaxPhase(enum CLKINDEX ind) {
   if (ind < 0 || ind >= NUM_CLOCKS) {
		LOG(logERROR, ("Unknown clock index %d to get max phase\n", ind));
	    return -1;
	}
	int vcofreq = getVCOFrequency(ind);
	int maxshiftstep = ALTERA_PLL_C10_GetMaxPhaseShiftStepsofVCO();
	int ret = ((double)vcofreq / (double)clkFrequency[ind]) * maxshiftstep;

	char* clock_names[] = {CLK_NAMES};
	LOG(logDEBUG1, ("\tMax Phase Shift (%s): %d (Clock: %d Hz, VCO:%d Hz)\n",
			clock_names[ind], ret, clkFrequency[ind], vcofreq));

	return ret;
}

int validatePhaseinDegrees(enum CLKINDEX ind, int val, int retval) {
   if (ind < 0 || ind >= NUM_CLOCKS) {
		LOG(logERROR, ("Unknown clock index %d to validate phase in degrees\n", ind));
	    return FAIL;
	}
	if (val == -1) {
		return OK;
	}
	LOG(logDEBUG1, ("validating phase in degrees for clk %d\n", (int)ind));
	int maxShift = getMaxPhase(ind);
	// convert degrees to shift
	int valShift = 0;
	ConvertToDifferentRange(0, 359, 0, maxShift - 1, val, &valShift);
	// convert back to degrees
	ConvertToDifferentRange(0, maxShift - 1, 0, 359, valShift, &val);

	if (val == retval)
		return OK;
	return FAIL;
}



int getFrequency(enum CLKINDEX ind) {
   if (ind < 0 || ind >= NUM_CLOCKS) {
		LOG(logERROR, ("Unknown clock index %d to get frequency\n", ind));
	    return -1;
	}
    return clkFrequency[ind];
}

int getVCOFrequency(enum CLKINDEX ind) {
   if (ind < 0 || ind >= NUM_CLOCKS) {
		LOG(logERROR, ("Unknown clock index %d to get vco frequency\n", ind));
	    return -1;
	}
	int pllIndex = (int)(ind >= SYSTEM_C0 ? SYSTEM_PLL : READOUT_PLL);
	return ALTERA_PLL_C10_GetVCOFrequency(pllIndex);
}

int getMaxClockDivider() {
	return ALTERA_PLL_C10_GetMaxClockDivider();
}

int setClockDivider(enum CLKINDEX ind, int val) {
   if (ind < 0 || ind >= NUM_CLOCKS) {
		LOG(logERROR, ("Unknown clock index %d to set clock divider\n", ind));
	    return FAIL;
	}
	if (val < 2 || val > getMaxClockDivider()) {
		return FAIL;
	}
	char* clock_names[] = {CLK_NAMES};
	int vcofreq = getVCOFrequency(ind);
	int currentdiv = vcofreq / (int)clkFrequency[ind];
	int newfreq = vcofreq / val;

    LOG(logINFO, ("\tSetting %s clock (%d) divider from %d (%d Hz) to %d (%d Hz). \n\t(Vcofreq: %d Hz)\n", clock_names[ind], ind, currentdiv, clkFrequency[ind], val, newfreq, vcofreq));

    // Remembering old phases in degrees
    int oldPhases[NUM_CLOCKS];
	{ 
		int i = 0;
		for (i = 0; i < NUM_CLOCKS; ++i) {
			oldPhases	[i] = getPhase(i, 1);
			LOG(logDEBUG1, ("\tRemembering %s clock (%d) phase: %d degrees\n", clock_names[ind], ind, oldPhases[i]));
		}
	}

    // Calculate and set output frequency
	int pllIndex = (int)(ind >= SYSTEM_C0 ? SYSTEM_PLL : READOUT_PLL);
	int clkIndex = (int)(ind >= SYSTEM_C0 ? ind - SYSTEM_C0 : ind);
    ALTERA_PLL_C10_SetOuputFrequency (pllIndex, clkIndex, newfreq);
	clkFrequency[ind] = newfreq;
    LOG(logINFO, ("\t%s clock (%d) divider set to %d (%d Hz)\n", clock_names[ind], ind, val, clkFrequency[ind]));
	// update system frequency
	if (ind == SYSTEM_C0) {
		setTimingSource(getTimingSource());
	}
   
    // phase is reset by pll (when setting output frequency)
	if (ind >= READOUT_C0) {
    	clkPhase[READOUT_C0] = 0;
    	clkPhase[READOUT_C1] = 0;		
	} else {
    	clkPhase[SYSTEM_C0] = 0;
    	clkPhase[SYSTEM_C1] = 0;
    	clkPhase[SYSTEM_C2] = 0;
    	clkPhase[SYSTEM_C3] = 0;
	}

    // set the phase in degrees (reset by pll)
	{ 
		int i = 0;
		for (i = 0; i < NUM_CLOCKS; ++i) {
			int currPhaseDeg = getPhase(i, 1);
			if (oldPhases[i] != currPhaseDeg) {
				LOG(logINFO, ("\tCorrecting %s clock (%d) phase from %d to %d degrees\n", clock_names[i], i, currPhaseDeg, oldPhases[i]));
				setPhase(i, oldPhases[i], 1);
			}
		}
	}
	return OK;
}

int getClockDivider(enum CLKINDEX ind) {
   if (ind < 0 || ind >= NUM_CLOCKS) {
		LOG(logERROR, ("Unknown clock index %d to get clock divider\n", ind));
	    return -1;
	}
	return (getVCOFrequency(ind) / (int)clkFrequency[ind]);
}

int setInjectChannel(int offset, int increment) {
	if (offset < 0 || increment  < 1) {
		LOG(logERROR, ("Cannot inject channel. Invalid offset %d or increment %d\n", offset, increment));
		return FAIL;
	}

	LOG(logINFO, ("Injecting channels [offset:%d, increment:%d]\n", offset, increment));
	
	// 4 bits of padding + 128 bits + 4 bits for address = 136 bits
	char buffer[17]; 
	memset(buffer, 0, sizeof(buffer));
	int startCh = 4; // 4 due to padding
	int ich = 0; 
	for (ich = startCh + offset; ich < startCh + NCHAN; ich = ich + increment) {
		int byteIndex = ich / 8;
		int bitIndex = ich % 8;
		buffer[byteIndex] |= (1 << (8 - 1 - bitIndex));
	}

	// address at the end
	buffer[16] |= (ASIC_CURRENT_INJECT_ADDR);

	int chipIndex = -1; // for all chips
	if (ASIC_Driver_Set(chipIndex, sizeof(buffer), buffer) == FAIL) {
		return FAIL;				
	}

	injectedChannelsOffset = offset;
	injectedChannelsIncrement = increment;
	return OK;
}

void getInjectedChannels(int* offset, int* increment) {
	*offset = injectedChannelsOffset;
	*increment = injectedChannelsIncrement;
}

int	setVetoReference(int gainIndex, int value) {
	LOG(logINFO, ("Setting veto reference [chip:-1, G%d, value:0x%x]\n", gainIndex, value));
	int vals[NCHAN];
	memset(vals, 0, sizeof(vals));
	int ich = 0;
	for (ich = 0; ich < NCHAN; ++ich) {
		vals[ich] = value;
	}
	return setVetoPhoton(-1, gainIndex, vals);
}

int	setVetoPhoton(int chipIndex, int gainIndex, int* values) {
	LOG(logINFO, ("Setting veto photon [chip:%d, G%d]\n", chipIndex, gainIndex));

	// add gain bits
	{
		int gainValue = 0;
		switch (gainIndex) {
			case 0:
				gainValue = ASIC_G0_VAL;
				break;
			case 1:
				gainValue = ASIC_G1_VAL;
				break;
			case 2:
				gainValue = ASIC_G2_VAL;
				break;	
			default:
				LOG(logERROR, ("Unknown gain index %d\n", gainIndex));
				return FAIL;			
		}
		LOG(logDEBUG2, ("Adding gain bits\n"));
		int i = 0;
		for (i = 0; i < NCHAN; ++i) {
			values[i] |= gainValue;
			LOG(logDEBUG2, ("Value %d: 0x%x\n", i, values[i]));
		}
	}

	const int lenDataBitsPerchannel = ASIC_GAIN_MAX_BITS + ADU_MAX_BITS; // 14
	const int lenBits = lenDataBitsPerchannel * NCHAN; // 1792
	const int padding  = 4; // due to address (4) to make it byte aligned
	const int lenTotalBits =  padding + lenBits + ASIC_ADDR_MAX_BITS; // 1800 
	const int len = lenTotalBits / 8; // 225

	// assign each bit into 4 + 1792  into byte array
	uint8_t commandBytes[lenTotalBits];
	memset(commandBytes, 0, sizeof(commandBytes));
	int offset = padding; // bit offset for commandbytes
	int ich = 0;
	for (ich = 0; ich < NCHAN; ++ich) {
		// loop through all bits in a value
		int iBit = 0; 
		for (iBit = 0; iBit < lenDataBitsPerchannel; ++iBit) {
			commandBytes[offset++] = ((values[ich] >> (lenDataBitsPerchannel - 1 - iBit)) & 0x1);
		}
	}

	// create command for 4 padding  + 1792 bits + 4 bits address = 1800 bits = 225 bytes
	char buffer[len];
	memset(buffer, 0, len);
	offset = 0;
	// loop through buffer elements
	for (ich = 0; ich < len; ++ich) {
		// loop through each bit in buffer element
		int iBit = 0; 
		for (iBit = 0; iBit < 8; ++iBit) {
			buffer[ich] |= (commandBytes[offset++] << (8 - 1 - iBit));
		}
	}

	// address at the end
	buffer[len - 1] |= (ASIC_VETO_REF_ADDR);

	if (ASIC_Driver_Set(chipIndex, sizeof(buffer), buffer) == FAIL) {
		return FAIL;				
	}

	// all chips
	if (chipIndex == -1) {
		int ichip = 0;
		int ichan = 0;
		for (ichan = 0; ichan < NCHAN; ++ichan) {
			for (ichip = 0; ichip < NCHIP; ++ichip) {
				vetoReference[ichip][ichan] = values[ichan];
			}
		}
	} 
	
	// specific chip
	else {
		int ichan = 0;
		for (ichan = 0; ichan < NCHAN; ++ichan) {
			vetoReference[chipIndex][chipIndex] = values[ichan];;
		}
	}
	return OK;
} 

int getVetoPhoton(int chipIndex, int* retvals) {
	if (chipIndex == -1) {
		int i = 0, j = 0;
		for	(i = 0; i < NCHAN; ++i) {
			int val = vetoReference[0][i];
			for (j = 1; j < NCHIP; ++j) {
				if (vetoReference[j][i] != val) {
					LOG(logERROR, ("Get vet photon fail for chipIndex:%d. Different values between [nchip:%d, nchan:%d, value:%d] and [nchip:0, nchan:%d, value:%d]\n", chipIndex, j, i, vetoReference[j][i], i, val));
					return FAIL;
				}
			}
		}
		chipIndex = 0;
	}
	memcpy((char*)retvals, ((char*)vetoReference) + NCHAN * chipIndex * sizeof(int), sizeof(int) * NCHAN);
	return OK;
}

int configureSingleADCDriver(int chipIndex) {
	LOG(logINFO, ("Configuring ADC for %s chips [chipIndex:%d Burst Mode:%d]\n", chipIndex == -1 ? "All" : "Single", chipIndex, burstMode));

	int ind = chipIndex;
	if (ind == -1) {
		ind = 0;
	}
	uint8_t values[NADC];
	memcpy(values, adcConfiguration + ind * NADC, NADC);

	// change adc values if continuous mode
	{
		int i = 0;
		for (i = 0; i < NADC; ++i) {
			if (burstMode == BURST_OFF) {
				values[i] |= ASIC_CONTINUOUS_MODE_MSK;
			}
			LOG(logDEBUG2, ("Value %d: 0x%02hhx\n", i, values[i]));
		}
	}


	const int lenDataBitsPerADC = ASIC_ADC_MAX_BITS; // 7
	const int lenBits = lenDataBitsPerADC * NADC; // 224
	const int padding  = 4; // due to address (4) to make it byte aligned
	const int lenTotalBits =  padding + lenBits + ASIC_ADDR_MAX_BITS; // 232 
	const int len = lenTotalBits / 8; // 29

	// assign each bit into 4 + 224  into byte array
	uint8_t commandBytes[lenTotalBits];
	memset(commandBytes, 0, sizeof(commandBytes));
	int offset = padding; // bit offset for commandbytes
	int ich = 0;
	for (ich = 0; ich < NADC; ++ich) {
		// loop through all bits in a value
		int iBit = 0; 
		for (iBit = 0; iBit < lenDataBitsPerADC; ++iBit) {
			commandBytes[offset++] = ((values[ich] >> (lenDataBitsPerADC - 1 - iBit)) & 0x1);
		}
	}

	// create command for 4 padding + 224 bits + 4 bits address = 232 bits = 29 bytes
	char buffer[len];
	memset(buffer, 0, len);
	offset = 0;
	// loop through buffer elements
	for (ich = 0; ich < len; ++ich) {
		// loop through each bit in buffer element
		int iBit = 0; 
		for (iBit = 0; iBit < 8; ++iBit) {
			buffer[ich] |= (commandBytes[offset++] << (8 - 1 - iBit));
		}
	}

	// address at the end
	buffer[len - 1] |= (ASIC_CONF_ADC_ADDR);

	if (ASIC_Driver_Set(chipIndex, sizeof(buffer), buffer) == FAIL) {
		return FAIL;				
	}

	return OK;
}

int configureADC() {
	LOG(logINFO, ("Configuring ADC \n"));

	int equal = 1;
	{
		int i = 0, j = 0;
		for	(i = 0; i < NADC; ++i) {
			int val = adcConfiguration[0][i];
			for (j = 1; j < NCHIP; ++j) {
				if (adcConfiguration[j][i] != val) {
					equal = 0;
					break;
				}
			}
		}
	}
	if (equal) {
		return configureSingleADCDriver(-1);
	} else {
		int i = 0;
		for (i = 0; i < NCHIP; ++i) {
			if (configureSingleADCDriver(i) == FAIL) {
				return FAIL;
			}
		}
	}
	return OK;
}

int	setBurstModeinFPGA(enum burstMode value) {
	uint32_t addr = ASIC_CONFIG_REG;
	uint32_t runmode = 0;
	switch (value) {
		case BURST_OFF:
			runmode = ASIC_CONFIG_RUN_MODE_CONT_VAL;
			break;
		case BURST_INTERNAL:
			runmode = ASIC_CONFIG_RUN_MODE_INT_BURST_VAL;
			break;
		case BURST_EXTERNAL:
			runmode = ASIC_CONFIG_RUN_MODE_EXT_BURST_VAL;
			break;
		default:
			LOG(logERROR, ("Unknown burst mode %d\n", value));
			return FAIL;
	}
	LOG(logDEBUG1, ("Run mode (FPGA val): %d\n", runmode));
	bus_w(addr, bus_r(addr) &~ ASIC_CONFIG_RUN_MODE_MSK);
	bus_w(addr, bus_r(addr) | ((runmode << ASIC_CONFIG_RUN_MODE_OFST) & ASIC_CONFIG_RUN_MODE_MSK));
	burstMode = value;
	return OK;
}

int	setBurstMode(enum burstMode burst) {
	LOG(logINFO, ("Setting burst mode to %s\n", burst == BURST_OFF ? "off" : (burst == BURST_INTERNAL ? "internal" : "external")));

	// update
	int64_t framesReg = 0;
	int64_t periodReg = 0;
	// burst
	if (burstMode != BURST_OFF) {
		framesReg =  ((bus_r(ASIC_INT_FRAMES_REG) & ASIC_INT_FRAMES_MSK) >> ASIC_INT_FRAMES_OFST);
		periodReg = get64BitReg(ASIC_INT_PERIOD_LSB_REG, ASIC_INT_PERIOD_MSB_REG);
		// auto
		if (getTiming() == AUTO_TIMING) {
			numBurstsReg = get64BitReg(SET_FRAMES_LSB_REG, SET_FRAMES_MSB_REG);
			burstPeriodReg = get64BitReg(SET_PERIOD_LSB_REG, SET_PERIOD_MSB_REG);
		}
	}	
	// continuous
	else {
		framesReg = get64BitReg(SET_FRAMES_LSB_REG, SET_FRAMES_MSB_REG);
		periodReg = get64BitReg(SET_PERIOD_LSB_REG, SET_PERIOD_MSB_REG);
	}

	if (setBurstModeinFPGA(burst) == FAIL) {
		return FAIL;
	}

	LOG(logINFO, ("\tUpdating registers\n"));
	// continuous
	if (burstMode == BURST_OFF) {
		set64BitReg(framesReg, SET_FRAMES_LSB_REG, SET_FRAMES_MSB_REG);
    	set64BitReg(periodReg, SET_PERIOD_LSB_REG, SET_PERIOD_MSB_REG);
		LOG(logINFO, ("\tFrames reg: %lld, Period reg: %lldns\n", getNumFrames(), getPeriod()));

		LOG(logINFO, ("\tInt. Frame reg: 1, Int. Period reg: 0\n"))
		bus_w(ASIC_INT_FRAMES_REG, bus_r(ASIC_INT_FRAMES_REG) &~ ASIC_INT_FRAMES_MSK);
		bus_w(ASIC_INT_FRAMES_REG, bus_r(ASIC_INT_FRAMES_REG) | ((1 << ASIC_INT_FRAMES_OFST) & ASIC_INT_FRAMES_MSK));
    	set64BitReg(0, ASIC_INT_PERIOD_LSB_REG, ASIC_INT_PERIOD_MSB_REG);
	}
	// burst
	else {
		bus_w(ASIC_INT_FRAMES_REG, bus_r(ASIC_INT_FRAMES_REG) &~ ASIC_INT_FRAMES_MSK);
		bus_w(ASIC_INT_FRAMES_REG, bus_r(ASIC_INT_FRAMES_REG) | (((int)framesReg << ASIC_INT_FRAMES_OFST) & ASIC_INT_FRAMES_MSK));
    	set64BitReg(periodReg, ASIC_INT_PERIOD_LSB_REG, ASIC_INT_PERIOD_MSB_REG);
		LOG(logINFO, ("\tInt. Frames reg: %lld, Int. Period reg: %lldns\n", getNumFrames(), getPeriod()));

		// trigger
		if (getTiming() == TRIGGER_EXPOSURE) {
			LOG(logINFO, ("\tFrame reg: 1, Period reg: 0\n"))
			set64BitReg(1, SET_FRAMES_LSB_REG, SET_FRAMES_MSB_REG);
    		set64BitReg(0, SET_PERIOD_LSB_REG, SET_PERIOD_MSB_REG);
		}
		//auto 
		else {
			set64BitReg(numBurstsReg, SET_FRAMES_LSB_REG, SET_FRAMES_MSB_REG);
    		set64BitReg(burstPeriodReg, SET_PERIOD_LSB_REG, SET_PERIOD_MSB_REG);
			LOG(logINFO, ("\tFrames reg (bursts): %lld, Period reg(burst period): %lldns\n", getNumBursts(), getBurstPeriod()));
		}
	}
	LOG(logINFO, ("\tDone Updating registers\n"))

	LOG(logINFO, ("\tSetting %s Mode in Chip\n", burstMode == BURST_OFF ? "Continuous" : "Burst"));
	int value = burstMode ? ASIC_GLOBAL_BURST_VALUE : ASIC_GLOBAL_CONT_VALUE;

	const int padding = 6; // due to address (4) to make it byte aligned
	const int lenTotalBits = padding + ASIC_GLOBAL_SETT_MAX_BITS + ASIC_ADDR_MAX_BITS; // 4 + 6 + 4 = 16
	const int len = lenTotalBits / 8; // 2

	// assign each bit into 4 + 224  into byte array
	uint8_t commandBytes[lenTotalBits];
	memset(commandBytes, 0, sizeof(commandBytes));
	int offset = padding; // bit offset for commandbytes
	int ich = 0;
	// loop through all bits in a value
	int iBit = 0; 
	for (iBit = 0; iBit < ASIC_GLOBAL_SETT_MAX_BITS; ++iBit) {
		commandBytes[offset++] = ((value >> (ASIC_GLOBAL_SETT_MAX_BITS - 1 - iBit)) & 0x1);
	}

	// create command for 4 padding + 224 bits + 4 bits address = 232 bits = 29 bytes
	char buffer[len];
	memset(buffer, 0, len);
	offset = 0;
	// loop through buffer elements
	for (ich = 0; ich < len; ++ich) {
		// loop through each bit in buffer element
		int iBit = 0; 
		for (iBit = 0; iBit < 8; ++iBit) {
			buffer[ich] |= (commandBytes[offset++] << (8 - 1 - iBit));
		}
	}

	// address at the end
	buffer[len - 1] |= (ASIC_CONF_GLOBAL_SETT);

	int chipIndex = -1;
	if (ASIC_Driver_Set(chipIndex, sizeof(buffer), buffer) == FAIL) {
		return FAIL;				
	}

	return configureADC();
}

enum burstMode getBurstMode() {
	uint32_t addr = ASIC_CONFIG_REG;
	int runmode = bus_r (addr) & ASIC_CONFIG_RUN_MODE_MSK;
	switch (runmode) {
		case ASIC_CONFIG_RUN_MODE_CONT_VAL:
			burstMode = BURST_OFF;
			break;
		case ASIC_CONFIG_RUN_MODE_INT_BURST_VAL:
			burstMode = BURST_INTERNAL;
			break;
		case ASIC_CONFIG_RUN_MODE_EXT_BURST_VAL:
			burstMode = BURST_EXTERNAL;
			break;
		default:
			LOG(logERROR, ("Unknown run mode read from FPGA %d\n", runmode));
			return -1;
	}
	return burstMode;
}

void setCurrentSource(int value) {
	uint32_t addr = ASIC_CONFIG_REG;
	if (value > 0) {
		bus_w(addr, (bus_r(addr) | ASIC_CONFIG_CURRENT_SRC_EN_MSK));
	} else if (value == 0) {
		bus_w(addr, (bus_r(addr) &~ ASIC_CONFIG_CURRENT_SRC_EN_MSK));
	}
}

int	getCurrentSource() {
	return ((bus_r(ASIC_CONFIG_REG) & ASIC_CONFIG_CURRENT_SRC_EN_MSK) >> ASIC_CONFIG_CURRENT_SRC_EN_OFST);
}

void setTimingSource(enum timingSourceType value) {
	uint32_t addr = CONTROL_REG;
	switch (value) {
		case TIMING_INTERNAL:
			LOG(logINFO, ("Setting timing source to internal\n"));
			bus_w(addr, (bus_r(addr) &~ CONTROL_TIMING_SOURCE_EXT_MSK));
			systemFrequency = INT_SYSTEM_C0_FREQUENCY;
			break;
		case TIMING_EXTERNAL:
			LOG(logINFO, ("Setting timing source to exernal\n"));
			bus_w(addr, (bus_r(addr) | CONTROL_TIMING_SOURCE_EXT_MSK));
			systemFrequency = clkFrequency[SYSTEM_C0];
			break;		
		default:
			LOG(logERROR, ("Unknown timing source %d\n", value));
			break;	
	}
}

enum timingSourceType getTimingSource() {
	if (bus_r(CONTROL_REG) & CONTROL_TIMING_SOURCE_EXT_MSK) {
		return TIMING_EXTERNAL;
	}
	return TIMING_INTERNAL;
}



/* aquisition */

int startStateMachine(){
#ifdef VIRTUAL
	// create udp socket
	if(createUDPSocket(0) != OK) {
		return FAIL;
	}
	LOG(logINFOBLUE, ("Starting State Machine\n"));
	// set status to running
	virtual_status = 1;
	virtual_stop = 0;
	if(pthread_create(&pthread_virtual_tid, NULL, &start_timer, NULL)) {
		LOG(logERROR, ("Could not start Virtual acquisition thread\n"));
		virtual_status = 0;
		return FAIL;
	}
	LOG(logINFOGREEN, ("Virtual Acquisition started\n"));
	return OK;
#endif
	LOG(logINFOBLUE, ("Starting State Machine\n"));
	cleanFifos();
	
	//start state machine
	bus_w(CONTROL_REG, bus_r(CONTROL_REG) | CONTROL_STRT_ACQSTN_MSK);

	LOG(logINFO, ("Status Register: %08x\n",bus_r(STATUS_REG)));
    return OK;
}


#ifdef VIRTUAL
void* start_timer(void* arg) {
	int numRepeats = getNumTriggers();
	if (getTiming() == AUTO_TIMING) {
		if (burstMode == BURST_OFF) {
			numRepeats = 1;
		} else {
			numRepeats = getNumBursts();
		}
	}
	int repeatPeriodNs = getBurstPeriod();
	int numFrames = getNumFrames();
	int64_t periodNs = getPeriod();
	int64_t expUs = getExpTime() / 1000;
	int imagesize = NCHAN * NCHIP * 2;
	int datasize = imagesize;
	int packetsize = datasize + sizeof(sls_detector_header);

	// Generate data
	char imageData[imagesize];
	memset(imageData, 0, imagesize);
	{
		int i = 0;
		for (i = 0; i < imagesize; i += sizeof(uint16_t)) {
			*((uint16_t*)(imageData + i)) = i;
		}
	}

	{
		int repeatNr = 0;
		int frameHeaderNr = 0;
		// loop over number of repeats
    	for(repeatNr=0; repeatNr!= numRepeats; ++repeatNr ) {

			struct timespec rbegin, rend;
    		clock_gettime(CLOCK_REALTIME, &rbegin);

    		int frameNr = 0;
			// loop over number of frames
    		for(frameNr = 0; frameNr != numFrames; ++frameNr ) {
			
				//check if virtual_stop is high
				if(virtual_stop == 1){
					break;
				}

				// sleep for exposure time
    		    struct timespec begin, end;
    		    clock_gettime(CLOCK_REALTIME, &begin);
    		    usleep(expUs);

				char packetData[packetsize];
				memset(packetData, 0, packetsize);
				// set header
				sls_detector_header* header = (sls_detector_header*)(packetData);
    		    header->detType = (uint16_t)myDetectorType;
    		    header->version = SLS_DETECTOR_HEADER_VERSION - 1;										
				header->frameNumber = frameHeaderNr;
				++frameHeaderNr;
				header->packetNumber = 0;
				header->modId = 0;
				header->row = detPos[X];
				header->column = detPos[Y];

				// fill data	
				memcpy(packetData + sizeof(sls_detector_header), imageData, datasize)		;	

				// send 1 packet = 1 frame
				sendUDPPacket(0, packetData, packetsize);

    		    clock_gettime(CLOCK_REALTIME, &end);
				LOG(logINFO, ("Sent frame: %d (bursts: %d)\n", frameNr, repeatNr));
    		    int64_t timeNs = ((end.tv_sec - begin.tv_sec) * 1E9 +
    		            (end.tv_nsec - begin.tv_nsec));

				// sleep for (period - exptime)
				if (frameNr < numFrames) { // if there is a next frame
					if (periodNs > timeNs) {
						usleep((periodNs - timeNs)/ 1000);
					}
				}
    		}
			clock_gettime(CLOCK_REALTIME, &rend);
			int64_t timeNs = ((rend.tv_sec - rbegin.tv_sec) * 1E9 +
					(rend.tv_nsec - rbegin.tv_nsec));

			// sleep for (repeatPeriodNs - time remaining)
			if (repeatNr < numRepeats) { // if there is a next repeat
				if (repeatPeriodNs > timeNs) {
					usleep((repeatPeriodNs - timeNs)/ 1000);
				}
			}

		}
	}

	closeUDPSocket(0);

	virtual_status = 0;
	LOG(logINFOBLUE, ("Finished Acquiring\n"));
	return NULL;
}
#endif


int stopStateMachine(){
	LOG(logINFORED, ("Stopping State Machine\n"));
#ifdef VIRTUAL
	virtual_stop = 0;
	return OK;
#endif
    //stop state machine
	bus_w(CONTROL_REG, bus_r(CONTROL_REG) | CONTROL_STP_ACQSTN_MSK);
	LOG(logINFO, ("Status Register: %08x\n", bus_r(STATUS_REG)));
    return OK;
}

enum runStatus getRunStatus(){
#ifdef VIRTUAL
	if(virtual_status == 0){
		LOG(logINFOBLUE, ("Status: IDLE\n"));
		return IDLE;
	}else{
		LOG(logINFOBLUE, ("Status: RUNNING\n"));
		return RUNNING;
	}
#endif
	LOG(logDEBUG1, ("Getting status\n"));
	uint32_t retval = bus_r(FLOW_STATUS_REG);
	LOG(logINFO, ("Status Register: %08x\n",retval));

	enum runStatus s;

	//running
	if (retval & FLOW_STATUS_RUN_BUSY_MSK) {
		if (retval & FLOW_STATUS_WAIT_FOR_TRGGR_MSK) {
			LOG(logINFOBLUE, ("Status: WAITING\n"));
			s = WAITING;
		} else {
			if (retval & FLOW_STATUS_DLY_BFRE_TRGGR_MSK) {
				LOG(logINFO, ("Status: Delay before Trigger\n"));
			} else if (retval & FLOW_STATUS_DLY_AFTR_TRGGR_MSK) {
				LOG(logINFO, ("Status: Delay after Trigger\n"));
			}
			LOG(logINFOBLUE, ("Status: RUNNING\n"));
			s = RUNNING;
		}
	}

	//not running
	else {
	    // stopped or error
		if (retval & FLOW_STATUS_FIFO_FULL_MSK) {
			LOG(logINFOBLUE, ("Status: STOPPED\n")); //FIFO FULL??
			s = STOPPED;
		} else if (retval & FLOW_STATUS_CSM_BUSY_MSK) {
			LOG(logINFOBLUE, ("Status: READ MACHINE BUSY\n"));
			s = TRANSMITTING;
		} else if (!retval) {
			LOG(logINFOBLUE, ("Status: IDLE\n"));
			s = IDLE;
		} else {
			LOG(logERROR, ("Status: Unknown status %08x\n", retval));
			s = ERROR;
		}
	}

	return s;
}

void readFrame(int *ret, char *mess) {
	// wait for status to be done
	while(runBusy()){
		usleep(500);
	}
#ifdef VIRTUAL
	LOG(logINFOGREEN, ("acquisition successfully finished\n"));
	return;
#endif

	*ret = (int)OK;
	// frames left to give status
	int64_t retval = getNumFramesLeft() + 1;

	if ( retval > 0) {
		LOG(logERROR, ("No data and run stopped: %lld frames left\n",(long  long int)retval));
	} else {
		LOG(logINFOGREEN, ("Acquisition successfully finished\n"));
	}
}

u_int32_t runBusy() {
#ifdef VIRTUAL
    return virtual_status;
#endif
	u_int32_t s = (bus_r(FLOW_STATUS_REG) & FLOW_STATUS_RUN_BUSY_MSK);
	//LOG(logDEBUG1, ("Status Register: %08x\n", s));
	return s;
}



/* common */

int calculateDataBytes() {
	return getTotalNumberOfChannels() * DYNAMIC_RANGE;
}

int getTotalNumberOfChannels() {return  (getNumberOfChannelsPerChip() * getNumberOfChips());}
int getNumberOfChips() {return  NCHIP;}
int getNumberOfDACs() {return  NDAC;}
int getNumberOfChannelsPerChip() {return  NCHAN;}