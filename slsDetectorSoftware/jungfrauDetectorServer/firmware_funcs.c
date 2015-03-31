//#define TESTADC
#define TESTADC1

//#define TIMEDBG 
#include "server_defs.h"
#include "firmware_funcs.h"
#include "mcb_funcs.h"
#include "registers_m.h"

//#define VERBOSE
//#define VERYVERBOSE


#ifdef SHAREDMEMORY
#include "sharedmemory.h"
#endif

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <sys/time.h>

struct timeval tss,tse,tsss; //for timing


//for memory mapping
u_int64_t CSP0BASE;

FILE *debugfp, *datafp;

int fr;
int wait_time;
int *fifocntrl;

//int *statusreg; commented out by dhanya
const int nModY=1;
int nModBoard;
int nModX=NMAXMOD;
int dynamicRange=16;//32;
int nSamples=1;

int dataBytes=NMAXMOD*NCHIP*NCHAN*2;

int storeInRAM=0;
int ROI_flag=0;
int adcConfigured=-1;
u_int16_t *ram_values=NULL;
volatile char *now_ptr=NULL;
volatile  u_int16_t *values;
int ram_size=0;

int64_t totalTime=1;
u_int32_t progressMask=0;

int phase_shift=0;//DEFAULT_PHASE_SHIFT;
int ipPacketSize=DEFAULT_IP_PACKETSIZE;
int udpPacketSize=DEFAULT_UDP_PACKETSIZE;

u_int32_t clkDivider[2]={32,16};
int32_t clkPhase[2]={0,0};


int ififostart, ififostop, ififostep, ififo;

int masterMode=NO_MASTER, syncMode=NO_SYNCHRONIZATION, timingMode=AUTO_TIMING;

enum externalSignalFlag  signals[4]={EXT_SIG_OFF, EXT_SIG_OFF, EXT_SIG_OFF, EXT_SIG_OFF};

int withGotthard = 0;

#ifdef MCB_FUNCS
extern const int nChans;
extern const int nChips;
//extern const int nDacs;
//extern const int nAdcs;
#endif
#ifndef MCB_FUNCS

const int nChans=NCHAN;
const int nChips=NCHIP;
const int nDacs=NDAC;
const int nAdcs=NADC;
#endif




/**
   ENEt conf structs
*/
typedef struct mac_header_struct{
  u_int8_t    mac_dest_mac2;
  u_int8_t    mac_dest_mac1;
  u_int8_t    mac_dummy1;
  u_int8_t    mac_dummy2;
  u_int8_t    mac_dest_mac6;
  u_int8_t    mac_dest_mac5;
  u_int8_t    mac_dest_mac4;
  u_int8_t    mac_dest_mac3;
  u_int8_t    mac_src_mac4;
  u_int8_t    mac_src_mac3;
  u_int8_t    mac_src_mac2;
  u_int8_t    mac_src_mac1;
  u_int16_t   mac_ether_type;
  u_int8_t    mac_src_mac6;
  u_int8_t    mac_src_mac5;
} mac_header;

typedef struct ip_header_struct {
  u_int16_t     ip_len;
  u_int8_t      ip_tos;
  u_int8_t      ip_ihl:4 ,ip_ver:4;
  u_int16_t     ip_offset:13,ip_flag:3;
  u_int16_t     ip_ident;
  u_int16_t     ip_chksum;
  u_int8_t      ip_protocol;
  u_int8_t      ip_ttl;
  u_int32_t     ip_sourceip;
  u_int32_t     ip_destip;
} ip_header;

typedef struct udp_header_struct{
  u_int16_t   udp_destport;
  u_int16_t   udp_srcport;
  u_int16_t   udp_chksum;
  u_int16_t   udp_len;
} udp_header;

typedef struct mac_conf_struct{
  mac_header  mac;
  ip_header   ip;
  udp_header  udp;
  u_int32_t   npack;
  u_int32_t   lpack;
  u_int32_t   npad;
  u_int32_t   cdone;
} mac_conf;

typedef struct tse_conf_struct{
  u_int32_t   rev;                    //0x0
  u_int32_t   scratch;
  u_int32_t   command_config;
  u_int32_t   mac_0;                  //0x3
  u_int32_t   mac_1;
  u_int32_t   frm_length;
  u_int32_t   pause_quant;
  u_int32_t   rx_section_empty;       //0x7
  u_int32_t   rx_section_full;
  u_int32_t   tx_section_empty;
  u_int32_t   tx_section_full;
  u_int32_t   rx_almost_empty;        //0xB
  u_int32_t   rx_almost_full;
  u_int32_t   tx_almost_empty;
  u_int32_t   tx_almost_full;
  u_int32_t   mdio_addr0;             //0xF
  u_int32_t   mdio_addr1;
}tse_conf;



int mapCSP0(void) {
  printf("Mapping memory\n");
#ifndef VIRTUAL
  int fd;
  fd = open("/dev/mem", O_RDWR | O_SYNC, 0);
  if (fd == -1) {
    printf("\nCan't find /dev/mem!\n");
       return FAIL;
  }
  printf("/dev/mem opened\n");

  CSP0BASE = (u_int32_t)mmap(0, MEM_SIZE, PROT_READ|PROT_WRITE, MAP_FILE|MAP_SHARED, fd, CSP0);
  if (CSP0BASE == (u_int32_t)MAP_FAILED) {
    printf("\nCan't map memmory area!!\n");
    return FAIL;
  }
  printf("CSP0 mapped\n");

#endif
#ifdef VIRTUAL
  CSP0BASE = malloc(MEM_SIZE);
  printf("memory allocated\n");
#endif
#ifdef SHAREDMEMORY 
  if ( (res=inism(SMSV))<0) {
    printf("error attaching shared memory! %i",res);
    return FAIL;
  }
#endif
  printf("CSPObase is 0x%x \n",CSP0BASE);
  printf("CSPOBASE=from %08x to %x\n",CSP0BASE,CSP0BASE+MEM_SIZE);

  u_int32_t address;
  address = FIFO_DATA_REG;//_OFF;
  values=(u_int16_t*)(CSP0BASE+address*2);
  printf("statusreg=%08x\n",bus_r(STATUS_REG));
  printf("\n\n");
  return OK;
}

u_int16_t bus_r16(u_int32_t offset){
  volatile u_int16_t *ptr1;
  ptr1=(u_int16_t*)(CSP0BASE+offset*2);
  return *ptr1;
}

u_int16_t bus_w16(u_int32_t offset, u_int16_t data) {
  volatile u_int16_t *ptr1;
  ptr1=(u_int16_t*)(CSP0BASE+offset*2);
  *ptr1=data;
  return OK;
}

/** ramType is DARK_IMAGE_REG or GAIN_IMAGE_REG */
u_int16_t ram_w16(u_int32_t ramType, int adc, int adcCh, int Ch, u_int16_t data) { 
  unsigned int adr = (ramType | adc << 8 | adcCh << 5 | Ch );
  // printf("Writing to addr:%x\n",adr);
  return bus_w16(adr,data);
}

/** ramType is DARK_IMAGE_REG or GAIN_IMAGE_REG */
u_int16_t ram_r16(u_int32_t ramType, int adc, int adcCh, int Ch){
  unsigned int adr = (ramType | adc << 8 | adcCh << 5 | Ch );
  //  printf("Reading from addr:%x\n",adr);
  return bus_r16(adr);
}

u_int32_t bus_w(u_int32_t offset, u_int32_t data) {
 volatile u_int32_t *ptr1;

  ptr1=(u_int32_t*)(CSP0BASE+offset*2);
  *ptr1=data;

  return OK;
}


u_int32_t bus_r(u_int32_t offset) {
  volatile u_int32_t *ptr1;

  ptr1=(u_int32_t*)(CSP0BASE+offset*2);
  return *ptr1;
}


int setPhaseShiftOnce(){
	u_int32_t addr, reg;
	int i;
	addr=MULTI_PURPOSE_REG;
	reg=bus_r(addr);
#ifdef VERBOSE
	printf("Multipurpose reg:%x\n",reg);
#endif

	//Checking if it is power on(negative number)
	// if(((reg&0xFFFF0000)>>16)>0){
	//bus_w(addr,0x0);   //clear the reg

	if(reg==0){
		printf("\nImplementing phase shift of %d\n",phase_shift);
		for (i=1;i<phase_shift;i++) {
			bus_w(addr,(INT_RSTN_BIT|ENET_RESETN_BIT|SW1_BIT|PHASE_STEP_BIT));//0x2821
			bus_w(addr,(INT_RSTN_BIT|ENET_RESETN_BIT|(SW1_BIT&~PHASE_STEP_BIT)));//0x2820
		}
#ifdef VERBOSE
		printf("Multipupose reg now:%x\n",bus_r(addr));
#endif
	}

	return OK;
}


int cleanFifo(){
/* 	u_int32_t addr, reg, val, adc_sync; */
/* 	printf("Cleaning FIFO\n"); */
/* 	addr=ADC_SYNC_REG; */

/* 	if(withGotthard) */
/* 		adc_sync = GOTTHARD_ADCSYNC_VAL; */
/* 	else */
/* 		adc_sync = ADCSYNC_VAL; */


/* 	reg = bus_r(addr) &	CLEAN_FIFO_MASK; */

/* 	//only for start up */
/* 	if(!reg) reg = adc_sync; */

/* 	// 88 3 02111 */
/* 	if (ROI_flag==0) { */
/* 	val=reg | ADCSYNC_CLEAN_FIFO_BITS | TOKEN_RESTART_DELAY; */
/* 	bus_w(addr,val); */
/* 	// 88 0 02111 */
/* 	val=reg | TOKEN_RESTART_DELAY; */
/* 	bus_w(addr,val); */
/* 	} */
/* 	else { */
/* 		//1b332214 */
/* 	  val=reg | ADCSYNC_CLEAN_FIFO_BITS | TOKEN_RESTART_DELAY_ROI; */
/* 	  bus_w(addr,val); */
/* 	  //1b032214 */
/* 	  val=reg | TOKEN_RESTART_DELAY_ROI; */
/* 	  bus_w(addr,val); */

/* 	} */
/* 	reg=bus_r(addr); */
/* //#ifdef DDEBUG */
/* 	printf("ADC SYNC reg 0x19:%x\n",reg); */
/* //#endif */
	return OK;
}


int setDAQRegister()
{
/* 	u_int32_t addr, reg, val; */
/* 	addr=DAQ_REG; */

/* 	//depended on adcval */
/* 	int packetlength=0x7f; */
/* 	if(!ROI_flag) packetlength=0x13f; */

/* 	//depended on pcb rev */
/* 	int tokenTiming = TOKEN_TIMING_REV2; */
/* 	if((bus_r(PCB_REV_REG)&BOARD_REVISION_MASK)==1) */
/* 		tokenTiming= TOKEN_TIMING_REV1; */


/* 	val = (packetlength<<16) + tokenTiming; */
/* 	//val=34+(42<<8)+(packetlength<<16); */

/* 	reg=bus_r(addr); */
/* 	bus_w(addr,val); */
/* 	reg=bus_r(addr); */
/* //#ifdef VERBOSE */
/* 	printf("DAQ reg 0x15:%x\n",reg); */
/* //#endif */

	return OK;
}


// direct pattern output 
u_int32_t putout(char *s, int modnum) {
  int i;
  u_int32_t pat;
  int addr;

  if (strlen(s)<16) {
    fprintf(stdout," *** putout error: incorrect pattern length ***\n");
    fprintf(stdout," %s \n",s);
    return FAIL;
  }

  pat=0;
  for (i=0;i<16;i++) {
    if (s[i]=='1') pat=pat+(1<<(15-i));
  }
  //addr=DAC_REG+(modnum<<4);
  addr=DAC_REG;//+(modnum<<SHIFTMOD); commented by dhanya
  bus_w(addr, pat);

  return OK;
}


// read direct input 
u_int32_t readin(int modnum) {
	return 0;
}

u_int32_t setPllReconfigReg(u_int32_t reg, u_int32_t val, int trig) {
 
  u_int32_t vv;




  // printf("*********** pll busy: %08x\n",bus_r(STATUS_REG)&PLL_RECONFIG_BUSY);

  bus_w(PLL_PARAM_REG,val); 
  //  printf("param: %x\n",val);


  vv=reg<<PLL_CNTR_ADDR_OFF;
  bus_w(PLL_CNTRL_REG,vv); 
  usleep(10000);
  //  printf("wrote: %08x\n",vv);
  //  vv=(1<<PLL_CNTR_WRITE_BIT)|(reg<<PLL_CNTR_ADDR_OFF)|(trig<<15);
  bus_w(PLL_CNTRL_REG,vv|(1<<PLL_CNTR_WRITE_BIT) );//15 is trigger for the tap
  // printf("----------- pll busy: %08x\n",bus_r(STATUS_REG)&PLL_RECONFIG_BUSY);
 //  printf("wrote: %08x\n",vv); 
 //  usleep(10000);


 // vv=(reg<<PLL_CNTR_ADDR_OFF);
  //  printf("wrote: %08x\n",vv); 
  bus_w(PLL_CNTRL_REG,vv); 
  usleep(10000);
  // printf("+++++++++ pll busy: %08x\n",bus_r(STATUS_REG)&PLL_RECONFIG_BUSY);

  //  bus_w(PLL_CNTRL_REG,(1<<PLL_CNTR_READ_BIT)|(reg<<PLL_CNTR_ADDR_OFF));
  // usleep(1000);
  // val=bus_r(PLL_PARAM_OUT_REG);
  // printf("counter %x reg: %x\n",reg,val); 
  // bus_w(PLL_CNTRL_REG,(reg<<PLL_CNTR_ADDR_OFF)); 

/*   usleep(100); */
/*   bus_w(PLL_CNTRL_REG,PLL_CNTR_PLL_RESET_BIT);  */
/*   usleep(100); */
/*   bus_w(PLL_CNTRL_REG,0);  */
  return val;

}

u_int32_t getPllReconfigReg(u_int32_t reg, int trig) {
  
  int i;
  u_int32_t val=reg<<PLL_CNTR_ADDR_OFF;
  u_int32_t vv;

  //  printf("cntrlreg: %08x\n",PLL_CNTRL_REG);
  // printf("wrote: %08x\n",val);
  bus_w(PLL_CNTRL_REG,val);
  //  printf("read: %08x\n",bus_r(PLL_CNTRL_REG));
  usleep(100);

  val=(1<<PLL_CNTR_READ_BIT)|(reg<<PLL_CNTR_ADDR_OFF)|(trig<<15);
  //  printf("wrote: %08x\n",val);
  bus_w(PLL_CNTRL_REG,val);//15 is trigger for the tap
  //  printf("read: %08x\n",bus_r(PLL_CNTRL_REG));
  //  usleep(100);
/*   for (i=0; i<10; i++) { */
  //   vv=bus_r(PLL_PARAM_OUT_REG); 
  //   printf("addr %x reg: %x\n",reg,vv); 
/*     usleep(100); */
/*   } */

  val=(reg<<PLL_CNTR_ADDR_OFF);
  //  printf("wrote: %08x\n",val);
  bus_w(PLL_CNTRL_REG,val);
  usleep(100);

  val=0;
  // printf("wrote: %08x\n",val);
  bus_w(PLL_CNTRL_REG,val);

  while(bus_r(STATUS_REG)&PLL_RECONFIG_BUSY) {
    printf("get: reconfig busy");
  }
  return vv;
}

void resetPLL() {
  bus_w(PLL_CNTRL_REG,(1<<PLL_CNTR_RECONFIG_RESET_BIT)|(1<<PLL_CNTR_PLL_RESET_BIT)); //reset PLL and pll reconfig
  usleep(100);
  bus_w(PLL_CNTRL_REG, 0);
}

void configurePll(int i) {


  u_int32_t l=0x0c;
  u_int32_t h=0x0d;
  u_int32_t val;
  int32_t phase=0, inv=0, ic=0;

  u_int32_t tot;
  u_int32_t odd=1;//0;

  //   printf("PLL reconfig reset\N");   bus_w(PLL_CNTRL_REG,(1<<PLL_CNTR_RECONFIG_RESET_BIT));  usleep(100);  bus_w(PLL_CNTRL_REG, 0);


  printf("PLL mode\n");   setPllReconfigReg(PLL_MODE_REG,1,0);
  //  usleep(10000);



  if (i<2) {

    tot= PLL_VCO_FREQ_MHZ/clkDivider[i];
    l=tot/2;
    h=l;
    if (tot>2*l) {
      h=l+1;
      odd=1;
    } 
    printf("Counter %d: Low is %d, High is %d\n",i, l,h);


  val= (i<<18)| (odd<<17) | l | (h<<8); 

  printf("Counter %d, val: %08x\n", i,  val);  
  setPllReconfigReg(PLL_C_COUNTER_REG, val,0);
  //  usleep(20);
  //change sync at the same time as 
  if (i>0) {
    val= (2<<18)| (odd<<17) | l | (h<<8); 

    printf("Counter %d, val: %08x\n", i,  val);  
    setPllReconfigReg(PLL_C_COUNTER_REG, val,0);
    
  }

  } else {
  //  if (mode==1) {
    //  } else {
    printf("phase in %d\n",clkPhase[1]);

  if (clkPhase[1]>0) {
    inv=0;
    phase=clkPhase[1];
  }  else {
    inv=1;
    phase=-1*clkPhase[1];
  }

  printf("phase out %d %08x\n",phase,phase);
  if (inv) {
    val=phase | (1<<16);// |  (inv<<21);
  printf("**************** phase word %08x\n",val);

  //  printf("Phase, val: %08x\n", val);   
  setPllReconfigReg(PLL_PHASE_SHIFT_REG,val,0); //shifts counter 0
  } else {


    val=phase ;// |  (inv<<21);
  printf("**************** phase word %08x\n",val);

  //  printf("Phase, val: %08x\n", val);   
  setPllReconfigReg(PLL_PHASE_SHIFT_REG,val,0); //shifts counter 0
    val=phase | (2<<16);// |  (inv<<21);
 printf("Start reconfig\n");  setPllReconfigReg(PLL_START_REG, 1,0);

 // bus_w(PLL_CNTRL_REG, 0);
 printf("Status register\n"); getPllReconfigReg(PLL_STATUS_REG,0);
  // sleep(1);
  
  printf("**************** phase word %08x\n",val);

  //  printf("Phase, val: %08x\n", val);   
  setPllReconfigReg(PLL_PHASE_SHIFT_REG,val,0); //shifts counter 0
  }
 

  }

 printf("Start reconfig\n");  setPllReconfigReg(PLL_START_REG, 1,0);

 // bus_w(PLL_CNTRL_REG, 0);
 printf("Status register\n"); getPllReconfigReg(PLL_STATUS_REG,0);
  // sleep(1);
  
  //  printf("PLL mode\n");   setPllReconfigReg(PLL_MODE_REG,0,0);
  usleep(10000);
  if (i<2) { 
     printf("reset pll\n"); 
     bus_w(PLL_CNTRL_REG,((1<<PLL_CNTR_PLL_RESET_BIT))); //reset PLL
     usleep(100);
     bus_w(PLL_CNTRL_REG, 0);
    
    
  } 
}









u_int32_t setClockDivider(int d, int ic) {


   u_int32_t l=0x0c;
   u_int32_t h=0x0d;
  u_int32_t val;
  int i;

  u_int32_t tot= PLL_VCO_FREQ_MHZ/d;
  u_int32_t odd=0;

  //	int ic=0  is run clk; ic=1 is adc clk 
  printf("set clk divider %d to %d\n", ic, d);
  if (ic>1)
    return -1;

  if (ic==1 && d>40)
    return -1;

  if (d>160)
    return -1;

  if (tot>510)
    return -1;

  if (tot<1)
    return -1;



  clkDivider[ic]=d;
  configurePll(ic);

  
  
 return clkDivider[ic];
}


int phaseStep(int st){
  
  int i;
  if (st>65535 || st<-65535) 
    return clkPhase[0];

  printf("phase %d\n",  st);

  clkPhase[1]=st-clkPhase[0];


  configurePll(2);
  clkPhase[0]=st;
 
  return clkPhase[0];
}


int getPhase() {
    return clkPhase[0];
    
};


u_int32_t getClockDivider(int ic) {

  if (ic>1)
    return -1;
  return clkDivider[ic];
  

/*   int ic=0; */
/*  u_int32_t val; */
/*  u_int32_t l,h; */

/*  printf("get clk divider\n"); */

  
/*   setPllReconfigReg(PLL_MODE_REG,1,0); */
/*   getPllReconfigReg(PLL_MODE_REG,0); */
  
/*   u_int32_t addr=0xa; //c0 */
/*   if (ic>0) */
/*     addr=0xb; //c1 */

/*   val=getPllReconfigReg(PLL_N_COUNTER_REG,0); */
/*   printf("Getting N counter %08x\n",val); */

/*   l=val&0xff; */
/*   h=(val>>8)&0xff; */

/*   //getPllReconfigReg(PLL_STATUS_REG,0); */
/*   val=getPllReconfigReg(addr,0); */
/*   printf("Getting C counter %08x\n",val); */

  

/*   return 800/(l+h); */

}


u_int32_t adcPipeline(int d) {
  if (d>=0)
    bus_w(DAQ_REG, d);
  return bus_r(DAQ_REG)&0xff;
}


u_int32_t setSetLength(int d) {
	 return 0;
}

u_int32_t getSetLength() {
	 return 0;
}

u_int32_t setOversampling(int d) {

  if (d>=0 && d<=255)
    bus_w(OVERSAMPLING_REG, d);

  return bus_r(OVERSAMPLING_REG);
}


u_int32_t setWaitStates(int d1) {
	 return 0;
}

u_int32_t getWaitStates() {
	 return 0;
}


u_int32_t setTotClockDivider(int d) {
	 return 0;
}

u_int32_t getTotClockDivider() {
	 return 0;
}


u_int32_t setTotDutyCycle(int d) {
	 return 0;
}

u_int32_t getTotDutyCycle() {
	 return 0;
}


u_int32_t setExtSignal(int d, enum externalSignalFlag  mode) {

	//int modes[]={EXT_SIG_OFF, EXT_GATE_IN_ACTIVEHIGH, EXT_GATE_IN_ACTIVELOW,EXT_TRIG_IN_RISING,EXT_TRIG_IN_FALLING,EXT_RO_TRIG_IN_RISING, EXT_RO_TRIG_IN_FALLING,EXT_GATE_OUT_ACTIVEHIGH, EXT_GATE_OUT_ACTIVELOW, EXT_TRIG_OUT_RISING, EXT_TRIG_OUT_FALLING, EXT_RO_TRIG_OUT_RISING, EXT_RO_TRIG_OUT_FALLING};
	// int off=d*SIGNAL_OFFSET;

	u_int32_t c;
	c=bus_r(EXT_SIGNAL_REG);

	if (d>=0 && d<4) {
		signals[d]=mode;
#ifdef VERBOSE
		printf("settings signal variable number %d to value %04x\n", d, signals[d]);
#endif

		// if output signal, set it!

		switch (mode) {
		case GATE_IN_ACTIVE_HIGH:
		case GATE_IN_ACTIVE_LOW:
			if (timingMode==GATE_FIX_NUMBER || timingMode==GATE_WITH_START_TRIGGER)
				setFPGASignal(d,mode);
			else
				setFPGASignal(d,SIGNAL_OFF);
			break;
		case TRIGGER_IN_RISING_EDGE:
		case TRIGGER_IN_FALLING_EDGE:
			if (timingMode==TRIGGER_EXPOSURE || timingMode==GATE_WITH_START_TRIGGER)
				setFPGASignal(d,mode);
			else
				setFPGASignal(d,SIGNAL_OFF);
			break;
		case RO_TRIGGER_IN_RISING_EDGE:
		case RO_TRIGGER_IN_FALLING_EDGE:
			if (timingMode==TRIGGER_READOUT)
				setFPGASignal(d,mode);
			else
				setFPGASignal(d,SIGNAL_OFF);
			break;
		case MASTER_SLAVE_SYNCHRONIZATION:
			setSynchronization(syncMode);
			break;
		default:
			setFPGASignal(d,mode);
			break;
		}

		setTiming(GET_EXTERNAL_COMMUNICATION_MODE);
	}


//  if (mode<=RO_TRIGGER_OUT_FALLING_EDGE && mode>=0)
//    bus_w(EXT_SIGNAL_REG,((modes[mode])<<off)|(c&~(SIGNAL_MASK<<off)));


  return getExtSignal(d);
}



u_int32_t setFPGASignal(int d, enum externalSignalFlag  mode) {


  int modes[]={EXT_SIG_OFF, EXT_GATE_IN_ACTIVEHIGH, EXT_GATE_IN_ACTIVELOW,EXT_TRIG_IN_RISING,EXT_TRIG_IN_FALLING,EXT_RO_TRIG_IN_RISING, EXT_RO_TRIG_IN_FALLING,EXT_GATE_OUT_ACTIVEHIGH, EXT_GATE_OUT_ACTIVELOW, EXT_TRIG_OUT_RISING, EXT_TRIG_OUT_FALLING, EXT_RO_TRIG_OUT_RISING, EXT_RO_TRIG_OUT_FALLING};

  u_int32_t c;
  int off=d*SIGNAL_OFFSET;
  c=bus_r(EXT_SIGNAL_REG);


  if (mode<=RO_TRIGGER_OUT_FALLING_EDGE &&  mode>=0) {
#ifdef VERBOSE
    printf("writing signal register number %d mode %04x\n",d, modes[mode]);
#endif
    bus_w(EXT_SIGNAL_REG,((modes[mode])<<off)|(c&~(SIGNAL_MASK<<off)));
  }
  return getExtSignal(d);
}



int getExtSignal(int d) {

/*  int modes[]={SIGNAL_OFF, GATE_IN_ACTIVE_HIGH, GATE_IN_ACTIVE_LOW,TRIGGER_IN_RISING_EDGE, TRIGGER_IN_FALLING_EDGE,RO_TRIGGER_IN_RISING_EDGE, RO_TRIGGER_IN_FALLING_EDGE, GATE_OUT_ACTIVE_HIGH,   GATE_OUT_ACTIVE_LOW, TRIGGER_OUT_RISING_EDGE, TRIGGER_OUT_FALLING_EDGE, RO_TRIGGER_OUT_RISING_EDGE,RO_TRIGGER_OUT_FALLING_EDGE};

    int off=d*SIGNAL_OFFSET;
    int mode=((bus_r(EXT_SIGNAL_REG)&(SIGNAL_MASK<<off))>>off);

    if (mode<RO_TRIGGER_OUT_FALLING_EDGE)
      return modes[mode];
    else 
      return -1;*/

	if (d>=0 && d<4) {
#ifdef VERBOSE
		printf("gettings signal variable number %d  value %04x\n", d, signals[d]);
#endif
		return signals[d];
	} else
		return -1;


}


int getFPGASignal(int d) {

  int modes[]={SIGNAL_OFF, GATE_IN_ACTIVE_HIGH, GATE_IN_ACTIVE_LOW,TRIGGER_IN_RISING_EDGE, TRIGGER_IN_FALLING_EDGE,RO_TRIGGER_IN_RISING_EDGE, RO_TRIGGER_IN_FALLING_EDGE, GATE_OUT_ACTIVE_HIGH,   GATE_OUT_ACTIVE_LOW, TRIGGER_OUT_RISING_EDGE, TRIGGER_OUT_FALLING_EDGE, RO_TRIGGER_OUT_RISING_EDGE,RO_TRIGGER_OUT_FALLING_EDGE};

    int off=d*SIGNAL_OFFSET;
    int mode=((bus_r(EXT_SIGNAL_REG)&(SIGNAL_MASK<<off))>>off);

    if (mode<=RO_TRIGGER_OUT_FALLING_EDGE) {
      if (modes[mode]!=SIGNAL_OFF && signals[d]!=MASTER_SLAVE_SYNCHRONIZATION)
	signals[d]=modes[mode];
#ifdef VERYVERBOSE
      printf("gettings signal register number %d  value %04x\n", d, modes[mode]);
#endif
      return modes[mode];
    } else
      return -1;

}





/*
enum externalCommunicationMode{
  GET_EXTERNAL_COMMUNICATION_MODE,
  AUTO,
  TRIGGER_EXPOSURE_SERIES,
  TRIGGER_EXPOSURE_BURST,
  TRIGGER_READOUT,
  TRIGGER_COINCIDENCE_WITH_INTERNAL_ENABLE,
  GATE_FIX_NUMBER,
  GATE_FIX_DURATION,
  GATE_WITH_START_TRIGGER,
  GATE_COINCIDENCE_WITH_INTERNAL_ENABLE
};
*/


int setTiming(int ti) {


  int ret=GET_EXTERNAL_COMMUNICATION_MODE;

  int g=-1, t=-1, rot=-1;

  int i;

  switch (ti) {
  case AUTO_TIMING:
    timingMode=ti;
    // disable all gates/triggers in except if used for master/slave synchronization
    for (i=0; i<4; i++) {
      if (getFPGASignal(i)>0 && getFPGASignal(i)<GATE_OUT_ACTIVE_HIGH && signals[i]!=MASTER_SLAVE_SYNCHRONIZATION)
	setFPGASignal(i,SIGNAL_OFF);
    }
    break;

  case   TRIGGER_EXPOSURE:
    timingMode=ti;
    // if one of the signals is configured to be trigger, set it and unset possible gates
    for (i=0; i<4; i++) {
      if (signals[i]==TRIGGER_IN_RISING_EDGE ||  signals[i]==TRIGGER_IN_FALLING_EDGE)
	setFPGASignal(i,signals[i]);
      else if (signals[i]==GATE_IN_ACTIVE_HIGH || signals[i]==GATE_IN_ACTIVE_LOW)
	setFPGASignal(i,SIGNAL_OFF);
      else if (signals[i]==RO_TRIGGER_IN_RISING_EDGE ||  signals[i]==RO_TRIGGER_IN_FALLING_EDGE)
	setFPGASignal(i,SIGNAL_OFF);

    }
    break;



  case  TRIGGER_READOUT:
    timingMode=ti;
    // if one of the signals is configured to be trigger, set it and unset possible gates
    for (i=0; i<4; i++) {
      if (signals[i]==RO_TRIGGER_IN_RISING_EDGE ||  signals[i]==RO_TRIGGER_IN_FALLING_EDGE)
	setFPGASignal(i,signals[i]);
      else if (signals[i]==GATE_IN_ACTIVE_HIGH || signals[i]==GATE_IN_ACTIVE_LOW)
	setFPGASignal(i,SIGNAL_OFF);
      else if (signals[i]==TRIGGER_IN_RISING_EDGE ||  signals[i]==TRIGGER_IN_FALLING_EDGE)
	setFPGASignal(i,SIGNAL_OFF);
    }
    break;

  case GATE_FIX_NUMBER:
    timingMode=ti;
    // if one of the signals is configured to be trigger, set it and unset possible gates
    for (i=0; i<4; i++) {
      if (signals[i]==RO_TRIGGER_IN_RISING_EDGE ||  signals[i]==RO_TRIGGER_IN_FALLING_EDGE)
	setFPGASignal(i,SIGNAL_OFF);
      else if (signals[i]==GATE_IN_ACTIVE_HIGH || signals[i]==GATE_IN_ACTIVE_LOW)
	setFPGASignal(i,signals[i]);
      else if (signals[i]==TRIGGER_IN_RISING_EDGE ||  signals[i]==TRIGGER_IN_FALLING_EDGE)
	setFPGASignal(i,SIGNAL_OFF);
    }
    break;



  case GATE_WITH_START_TRIGGER:
    timingMode=ti;
    for (i=0; i<4; i++) {
      if (signals[i]==RO_TRIGGER_IN_RISING_EDGE ||  signals[i]==RO_TRIGGER_IN_FALLING_EDGE)
	setFPGASignal(i,SIGNAL_OFF);
      else if (signals[i]==GATE_IN_ACTIVE_HIGH || signals[i]==GATE_IN_ACTIVE_LOW)
	setFPGASignal(i,signals[i]);
      else if (signals[i]==TRIGGER_IN_RISING_EDGE ||  signals[i]==TRIGGER_IN_FALLING_EDGE)
	setFPGASignal(i,signals[i]);
    }
    break;

  default:
	  break;

  }



  for (i=0; i<4; i++) {
    if (signals[i]!=MASTER_SLAVE_SYNCHRONIZATION) {
      if (getFPGASignal(i)==RO_TRIGGER_IN_RISING_EDGE ||  getFPGASignal(i)==RO_TRIGGER_IN_FALLING_EDGE)
	rot=i;
      else if (getFPGASignal(i)==GATE_IN_ACTIVE_HIGH || getFPGASignal(i)==GATE_IN_ACTIVE_LOW)
	g=i;
      else if (getFPGASignal(i)==TRIGGER_IN_RISING_EDGE ||  getFPGASignal(i)==TRIGGER_IN_FALLING_EDGE)
	t=i;
    }
  }


  if (g>=0 && t>=0 && rot<0) {
    ret=GATE_WITH_START_TRIGGER;
  } else if (g<0 && t>=0 && rot<0) {
    ret=TRIGGER_EXPOSURE;
  } else if (g>=0 && t<0 && rot<0) {
    ret=GATE_FIX_NUMBER;
  } else if (g<0 && t<0 && rot>0) {
    ret=TRIGGER_READOUT;
  } else if (g<0 && t<0 && rot<0) {
    ret=AUTO_TIMING;
  }

  // timingMode=ret;

  return ret;

}



int setConfigurationRegister(int d) {
#ifdef VERBOSE
  printf("Setting configuration register to %x",d);
#endif
  if (d>=0) {
    bus_w(CONFIG_REG,d);
  }
#ifdef VERBOSE
  printf("configuration register is %x", bus_r(CONFIG_REG));
#endif
  return bus_r(CONFIG_REG);
}

int setToT(int d) {
 //int ret=0;
 int reg;
#ifdef VERBOSE
  printf("Setting ToT to %d\n",d);
#endif
  reg=bus_r(CONFIG_REG);
#ifdef VERBOSE
  printf("Before: ToT is %x\n", reg);
#endif
  if (d>0) {
    bus_w(CONFIG_REG,reg|TOT_ENABLE_BIT);
  } else if (d==0) {
    bus_w(CONFIG_REG,reg&(~TOT_ENABLE_BIT));
  }
  reg=bus_r(CONFIG_REG);
#ifdef VERBOSE
  printf("ToT is %x\n", reg);
#endif
  if (reg&TOT_ENABLE_BIT)
    return 1;
  else
    return 0;
}

int setContinousReadOut(int d) {
 //int ret=0;
 int reg;
#ifdef VERBOSE
  printf("Setting Continous readout to %d\n",d);
#endif
  reg=bus_r(CONFIG_REG);
#ifdef VERBOSE
  printf("Before: Continous readout is %x\n", reg);
#endif
  if (d>0) {
    bus_w(CONFIG_REG,reg|CONT_RO_ENABLE_BIT);
  } else if (d==0) {
    bus_w(CONFIG_REG,reg&(~CONT_RO_ENABLE_BIT));
  }
  reg=bus_r(CONFIG_REG);
#ifdef VERBOSE
  printf("Continous readout is %x\n", reg);
#endif
  if (reg&CONT_RO_ENABLE_BIT)
    return 1;
  else
    return 0;
}


int startReceiver(int start) {
	u_int32_t addr=CONFIG_REG;
#ifdef VERBOSE
	if(start)
		printf("Setting up detector to send to Receiver\n");
	else
		printf("Setting up detector to send to CPU\n");
#endif
	int reg=bus_r(addr);
	//for start recever, write 0 and for stop, write 1
	if (!start)
		bus_w(CONFIG_REG,reg&(~GB10_NOT_CPU_BIT));
	else
		bus_w(CONFIG_REG,reg|GB10_NOT_CPU_BIT);

	reg=bus_r(addr);
//#ifdef VERBOSE
	printf("Config Reg %x\n", reg);
//#endif
	int d =reg&GB10_NOT_CPU_BIT;
	if(d!=0) d=1;
	if(d!=start)
		return OK;
	else
		return FAIL;
}


u_int64_t  getDetectorNumber() {
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
}

u_int32_t  getFirmwareVersion() {
  return bus_r(FPGA_VERSION_REG);
}

u_int32_t  getFirmwareSVNVersion(){
  return bus_r(FPGA_SVN_REG);
}


// for fpga test 
u_int32_t testFpga(void) {
	  printf("Testing FPGA:\n");
  volatile u_int32_t val,addr,val2;
  int result=OK,i;
  //fixed pattern
  val=bus_r(FIX_PATT_REG);
  if (val==FIXED_PATT_VAL) {
    printf("fixed pattern ok!! %08x\n",val);
  } else {
    printf("fixed pattern wrong!! %08x\n",val);
    result=FAIL;
  }

  //dummy register
  addr = DUMMY_REG;
  for(i=0;i<1000000;i++)
    {
      val=0x5A5A5A5A-i;
      bus_w(addr, val);
      val=bus_r(addr);
      if (val!=0x5A5A5A5A-i) {
	printf("ATTEMPT:%d:\tFPGA dummy register wrong!! %x instead of %x \n",i,val,0x5A5A5A5A-i);
	result=FAIL;
      }
      val=(i+(i<<10)+(i<<20));
      bus_w(addr, val);
      val2=bus_r(addr);
      if (val2!=val) {
	printf("ATTEMPT:%d:\tFPGA dummy register wrong!! read %x instead of %x.\n",i,val2,val);
	result=FAIL;
      }
      val=0x0F0F0F0F;
      bus_w(addr, val);
      val=bus_r(addr);
      if (val!=0x0F0F0F0F) {
	printf("ATTEMPT:%d:\tFPGA dummy register wrong!! %x instead of 0x0F0F0F0F \n",i,val);
	result=FAIL;
      }
      val=0xF0F0F0F0;
      bus_w(addr, val);
      val=bus_r(addr);
      if (val!=0xF0F0F0F0)  {
	printf("ATTEMPT:%d:\tFPGA dummy register wrong!! %x instead of 0xF0F0F0F0 \n\n",i,val);
	result=FAIL;
      }
    }
  if(result==OK)
    {
      printf("----------------------------------------------------------------------------------------------");
      printf("\nATTEMPT 1000000: FPGA DUMMY REGISTER OK!!!\n");
      printf("----------------------------------------------------------------------------------------------");
    }
  printf("\n");
  return result;
}


// for fpga test 
u_int32_t testRAM(void) {
  int result=OK;
  int i=0;
  allocateRAM();
  //  while(i<100000) {
    memcpy(ram_values, values, dataBytes);
    printf ("Testing RAM:\t%d: copied fifo %x to memory %x size %d\n",i++, (unsigned int)(values), (unsigned int)(ram_values), dataBytes);
    // }
  return result;
}

int getNModBoard() {
  return nModX;
}

int setNMod(int n) {
  return nModX;
}


// fifo test 
int testFifos(void) {
  printf("Fifo test not implemented!\n");
  bus_w16(CONTROL_REG, START_FIFOTEST_BIT);
  bus_w16(CONTROL_REG, 0x0);
  return OK;
}



// program dacq settings 

int64_t set64BitReg(int64_t value, int aLSB, int aMSB){
  int64_t v64;
  u_int32_t vLSB,vMSB;
  if (value!=-1) {
    vLSB=value&(0xffffffff);
    bus_w(aLSB,vLSB);
    v64=value>> 32;
    vMSB=v64&(0xffffffff);
    bus_w(aMSB,vMSB);
  }
  return get64BitReg(aLSB, aMSB);

}

int64_t get64BitReg(int aLSB, int aMSB){
  int64_t v64;
  u_int32_t vLSB,vMSB;
  vLSB=bus_r(aLSB);
  vMSB=bus_r(aMSB);
  v64=vMSB;
  v64=(v64<<32) | vLSB;

  printf("reg64(%x,%x) %x %x %llx\n", aLSB, aMSB, vLSB, vMSB, v64);

  return v64;
}

int64_t setFrames(int64_t value){
  return set64BitReg(value,  SET_FRAMES_LSB_REG, SET_FRAMES_MSB_REG);
}

int64_t getFrames(){
  printf("gf");
  return get64BitReg(GET_FRAMES_LSB_REG, GET_FRAMES_MSB_REG);
}

int64_t setExposureTime(int64_t value){
  /* time is in ns */
  if (value!=-1)
    value*=(1E-3*clkDivider[0]);//(1E-9*CLK_FREQ);
    return set64BitReg(value,SET_EXPTIME_LSB_REG, SET_EXPTIME_MSB_REG)/(1E-3*clkDivider[0]);//(1E-9*CLK_FREQ);
}

int64_t getExposureTime(){
  return get64BitReg(GET_EXPTIME_LSB_REG, GET_EXPTIME_MSB_REG)/(1E-3*clkDivider[0]);//(1E-9*CLK_FREQ);
}

int64_t setGates(int64_t value){
  return set64BitReg(value, SET_GATES_LSB_REG, SET_GATES_MSB_REG);
}

int64_t getGates(){
  return get64BitReg(GET_GATES_LSB_REG, GET_GATES_MSB_REG);
}

int64_t setPeriod(int64_t value){
  /* time is in ns */
  if (value!=-1) {
    // value*=(1E-9*CLK_FREQ);
    value*=(1E-3*clkDivider[0]);
  }



  return set64BitReg(value,SET_PERIOD_LSB_REG, SET_PERIOD_MSB_REG)/(1E-3*clkDivider[0]);//(1E-9*CLK_FREQ);
}

int64_t getPeriod(){
  return get64BitReg(GET_PERIOD_LSB_REG, GET_PERIOD_MSB_REG)/(1E-3*clkDivider[0]);//(1E-9*CLK_FREQ);
}

int64_t setDelay(int64_t value){
  /* time is in ns */
  if (value!=-1) {
    value*=(1E-3*clkDivider[0]);//(1E-9*CLK_FREQ);
  }
  return set64BitReg(value,SET_DELAY_LSB_REG, SET_DELAY_MSB_REG)/(1E-3*clkDivider[0]);//(1E-9*CLK_FREQ);
}

int64_t getDelay(){
  return get64BitReg(GET_DELAY_LSB_REG, GET_DELAY_MSB_REG)/(1E-3*clkDivider[0]);//(1E-9*CLK_FREQ);
}

int64_t setTrains(int64_t value){
  return set64BitReg(value,  SET_CYCLES_LSB_REG, SET_CYCLES_MSB_REG);
}

int64_t getTrains(){
  return get64BitReg(GET_CYCLES_LSB_REG, GET_CYCLES_MSB_REG);
}


int64_t setProbes(int64_t value){
  return 0;
}


int64_t setProgress() {

  //????? eventually call after setting the registers

return 0;

}


int64_t getProgress() {


  //should be done in firmware!!!!

	return 0;

}

int64_t getActualTime(){
  return get64BitReg(GET_ACTUAL_TIME_LSB_REG, GET_ACTUAL_TIME_MSB_REG)/(1E-9*CLK_FREQ);
}

int64_t getMeasurementTime(){
  int64_t v=get64BitReg(GET_MEASUREMENT_TIME_LSB_REG, GET_MEASUREMENT_TIME_MSB_REG);
  // int64_t mask=0x8000000000000000;
  // if (v & mask ) {
  //#ifdef VERBOSE
  //  printf("no measurement time left\n");
  //#endif
  //  return -1E+9;
  // } else
    return v/(1E-9*CLK_FREQ);
}

int64_t getFramesFromStart(){
  int64_t v=get64BitReg(FRAMES_FROM_START_LSB_REG, FRAMES_FROM_START_MSB_REG);
  int64_t v1=get64BitReg(FRAMES_FROM_START_PG_LSB_REG, FRAMES_FROM_START_PG_MSB_REG);

  printf("Frames from start data streaming %lld\n",v);
  printf("Frames from start run control %lld\n",v1);

  // int64_t mask=0x8000000000000000;
  // if (v & mask ) {
  //#ifdef VERBOSE
  //  printf("no measurement time left\n");
  //#endif
  //  return -1E+9;
  // } else
    return v;
}




int loadImage(int index, short int ImageVals[]){
	u_int32_t address;
	switch (index) {
	case DARK_IMAGE :
		address = DARK_IMAGE_REG;
		break;
	case GAIN_IMAGE :
		address = GAIN_IMAGE_REG;
		break;
	}
	volatile u_int16_t *ptr;
	ptr=(u_int16_t*)(CSP0BASE+address*2);
#ifdef VERBOSE
	int i;
	for(i=0;i<6;i++)
		printf("%d:%d\t",i,ImageVals[i]);
#endif
	memcpy(ptr,ImageVals ,dataBytes);
#ifdef VERBOSE
	printf("\nLoaded x%08x address with image of index %d\n",(unsigned int)(ptr),index);
#endif
	return OK;
}



int64_t getProbes(){
  return 0;
}


int setDACRegister(int idac, int val, int imod) {
  u_int32_t addr, reg, mask;
  int off;
#ifdef VERBOSE
  if(val==-1)
    printf("Getting dac register%d module %d\n",idac,imod);
  else
    printf("Setting dac register %d module %d to %d\n",idac,imod,val);
#endif

  switch(idac){
  case 0:
  case 1:
  case 2:
    addr=MOD_DACS1_REG;
    break;
  case 3:
  case 4:
  case 5:
    addr=MOD_DACS2_REG;
    break;
  case 6:
  case 7:
    addr=MOD_DACS3_REG;
    break;
  default:
    printf("weird idac value %d\n",idac);
    return -1;
    break;
  }
  //saving only the msb
  val=val>>2;

  off=(idac%3)*10;
  mask=~((0x3ff)<<off);

  if (val>=0 && val<DAC_DR) {
    reg=bus_r(addr+(imod<<SHIFTMOD));
      reg&=mask;
      reg|=(val<<off);
      bus_w(addr+(imod<<SHIFTMOD),reg);
  }
  val=(bus_r(addr+(imod<<SHIFTMOD))>>off)&0x3ff;
  //since we saved only the msb
  val=val<<2;

  //val=(bus_r(addr)>>off)&0x3ff;


#ifdef VERBOSE
  printf("Dac %d module %d register is %d\n\n",idac,imod,val);
#endif
   return val;
}


int getTemperature(int tempSensor, int imod){
  int val;
  imod=0;//ignoring more than 1 mod for now
  int i,j,repeats=6;
  u_int32_t tempVal=0;
#ifdef VERBOSE
  char cTempSensor[2][100]={"ADCs/ASICs","VRs/FPGAs"};
  printf("Getting Temperature of module:%d for the %s for tempsensor:%d\n",imod,cTempSensor[tempSensor],tempSensor);
#endif
  bus_w(TEMP_IN_REG,(T1_CLK_BIT)|(T1_CS_BIT)|(T2_CLK_BIT)|(T2_CS_BIT));//standby
  bus_w(TEMP_IN_REG,((T1_CLK_BIT)&~(T1_CS_BIT))|(T2_CLK_BIT));//high clk low cs

  for(i=0;i<20;i++) {
    //repeats is number of register writes for delay
    for(j=0;j<repeats;j++)
      bus_w(TEMP_IN_REG,~(T1_CLK_BIT)&~(T1_CS_BIT)&~(T2_CLK_BIT)&~(T2_CS_BIT));//low clk low cs
    for(j=0;j<repeats;j++)
      bus_w(TEMP_IN_REG,((T1_CLK_BIT)&~(T1_CS_BIT))|(T2_CLK_BIT));//high clk low cs

    if(i<=10){//only the first time
      if(!tempSensor)
	tempVal= (tempVal<<1) + (bus_r(TEMP_OUT_REG) & (1));//adc
      else
	tempVal= (tempVal<<1) + ((bus_r(TEMP_OUT_REG) & (2))>>1);//fpga
    }
  }

  bus_w(TEMP_IN_REG,(T1_CLK_BIT)|(T1_CS_BIT)|(T2_CLK_BIT)|(T2_CS_BIT));//standby
  val=((int)tempVal)/4.0;

#ifdef VERBOSE
   printf("Temperature of module:%d for the %s is %.2fC\n",imod,cTempSensor[tempSensor],val);
#endif
 return val;
}



int initHighVoltage(int val, int imod){

  printf("*******SetHV: Doing nothing - still to be implemented!\n");

/* #ifdef VERBOSE */
/*   printf("Setting/Getting High Voltage of module:%d with val:%d\n",imod,val); */
/* #endif */
/*   volatile u_int32_t addr=HV_REG; */
/*   int writeVal,writeVal2; */
/*   switch(val){ */
/*   case -1: break; */
/*   case 0:  writeVal=0x0; writeVal2=0x0; break; */
/*   case 90: writeVal=0x0; writeVal2=0x1; break; */
/*   case 110:writeVal=0x2; writeVal2=0x3; break; */
/*   case 120:writeVal=0x4; writeVal2=0x5; break; */
/*   case 150:writeVal=0x6; writeVal2=0x7; break; */
/*   case 180:writeVal=0x8; writeVal2=0x9; break; */
/*   case 200:writeVal=0xA; writeVal2=0xB; break; */
/*   default :printf("Invalid voltage\n");return -2;break; */
/*   } */
/*   //to set value */
/*   if(val!=-1){ */
/*     //set value to converted value */
/*     bus_w(addr,writeVal); */
/*     bus_w(addr,writeVal2); */
/* #ifdef VERBOSE */
/*     printf("Value sent is %d and then %d\n",writeVal,writeVal2); */
/* #endif  */
/*   } */
/*  //read value and return the converted value */
/*   val=bus_r(addr); */
/* #ifdef VERBOSE */
/*     printf("Value read from reg is %d\n",val); */
/* #endif  */
/*   switch(val){ */
/*   case 0x0:val=0;break; */
/*   case 0x1:val=90;break; */
/*   case 0x3:val=110;break; */
/*   case 0x5:val=120;break; */
/*   case 0x7:val=150;break; */
/*   case 0x9:val=180;break; */
/*   case 0xB:val=200;break; */
/*   default:printf("Weird value read:%d\n",val);return -3;break; */
/*   } */
/* #ifdef VERBOSE */
/*   printf("High voltage of module:%d is %d\n",imod,val); */
/* #endif   */
   return val;
}



int initConfGain(int isettings,int val,int imod){
  int retval;
  u_int32_t addr=GAIN_REG;

  if(val!=-1){
#ifdef VERBOSE
    printf("Setting Gain of module:%d with val:%d\n",imod,val);
#endif
    bus_w(addr,((val<<GAIN_OFFSET)|(bus_r(addr)&~GAIN_MASK)));
  }
  retval=(bus_r(addr)&GAIN_MASK);
#ifdef VERBOSE
  printf("Value read from Gain reg is %d\n",retval);
#endif 
  if((val!=-1)&&(retval!=val))
    return -1;

  if(isettings!=-1){
#ifdef VERBOSE
    printf("Writing Settings of module:%d with val:%d\n",imod,isettings);
#endif
    bus_w(addr,((isettings<<SETTINGS_OFFSET)|(bus_r(addr)&~SETTINGS_MASK)));
  }
      retval=((bus_r(addr)&SETTINGS_MASK)>>SETTINGS_OFFSET);
#ifdef VERBOSE
    printf("Settings read from reg is %d\n",retval);
#endif 
    if((isettings!=-1)&&(retval!=isettings)){
      printf("\n\nSettings r\n\n");
    return -1;
    }

   return retval;
}



int setADC(int adc){
	int reg,nchips,mask,nchans;

	if(adc==-1)	ROI_flag=0;
	else		ROI_flag=1;

	//	setDAQRegister();//token timing
	cleanFifo();//adc sync

	//with gotthard module
	if(withGotthard){
		//set packet size
		ipPacketSize= DEFAULT_IP_PACKETSIZE;
		udpPacketSize=DEFAULT_UDP_PACKETSIZE;
		//set channel mask
		nchips = GOTTHARDNCHIP;
		nchans = GOTTHARDNCHAN;
		mask = ACTIVE_ADC_MASK;
	}

	//with moench module all adc
	else{/* if(adc==-1){*/
		//set packet size
		ipPacketSize= DEFAULT_IP_PACKETSIZE;
		udpPacketSize=DEFAULT_UDP_PACKETSIZE;
		//set channel mask
		nchips = NCHIP;
		nchans = NCHANS;
		mask = ACTIVE_ADC_MASK;
	}/*
	//with moench module 1 adc -- NOT IMPLEMENTED
	else{
		ipPacketSize= ADC1_IP_PACKETSIZE;
		udpPacketSize=ADC1_UDP_PACKETSIZE;
		//set channel mask
		nchips = NCHIPS_PER_ADC;
		nchans = GOTTHARDNCHAN;
		mask = 1<<adc;
	}*/

	//set channel mask
	reg = (nchans*nchips)<<CHANNEL_OFFSET;
	reg&=CHANNEL_MASK;
	reg|=(ACTIVE_ADC_MASK & mask);
	bus_w(CHIP_OF_INTRST_REG,reg);

//#ifdef DDEBUG
	printf("Chip of Interest Reg:%x\n",bus_r(CHIP_OF_INTRST_REG));
//#endif

	adcConfigured = adc;

	return adcConfigured;
}



int configureMAC(int ipad,long long int macad,long long int detectormacad, int detipad, int ival, int udpport){


#ifdef DDEBUG
	printf("Chip of Intrst Reg:%x\n",bus_r(CHIP_OF_INTRST_REG));
	printf("IP Packet Size:%d\n",ipPacketSize);
	printf("UDP Packet Size:%d\n",udpPacketSize);
#endif

	//configuring mac
	u_int32_t addrr=MULTI_PURPOSE_REG;
	u_int32_t offset=ENET_CONF_REG, offset2=TSE_CONF_REG;
	mac_conf *mac_conf_regs;
	tse_conf *tse_conf_regs;
	long int sum = 0;
	long int checksum;
	int count,val;
	unsigned short *addr;

	mac_conf_regs=(mac_conf*)(CSP0BASE+offset*2);
	tse_conf_regs=(tse_conf*)(CSP0BASE+offset2*2);

#ifdef DDEBUG
	printf("***Configuring MAC*** \n");
#endif

	if(ival)
		bus_w(addrr,(RESET_BIT|DIGITAL_TEST_BIT)); //0x080,reset mac (reset)
	else
		bus_w(addrr,RESET_BIT); //0x080,reset mac (reset)
	val=bus_r(addrr);
#ifdef VERBOSE
	printf("Value read from Multi-purpose Reg:%x\n",val);
#endif 
	//  if(val!=0x080) return -1;

	usleep(500000);

	if(ival)
		bus_w(addrr,(ENET_RESETN_BIT|WRITE_BACK_BIT|DIGITAL_TEST_BIT)); //0x840,write shadow regs(enet reset,write bak)
	else
		bus_w(addrr,(ENET_RESETN_BIT|WRITE_BACK_BIT)); //0x840,write shadow regs(enet reset,write bak)
	val=bus_r(addrr);
#ifdef VERBOSE
	printf("Value read from Multi-purpose Reg:%x\n",val);
#endif 
	//  if(val!=0x840) return -1;

	if(ival)
		bus_w(addrr,(ENET_RESETN_BIT|DIGITAL_TEST_BIT)); //0x800,nreset phy(enet reset)
	else
		bus_w(addrr,ENET_RESETN_BIT); //0x800,nreset phy(enet reset)
	val=bus_r(addrr);
#ifdef VERBOSE
	printf("Value read from Multi-purpose Reg:%x\n",val);
#endif 
	//  if(val!=0x800) return -1;


	mac_conf_regs->mac.mac_dest_mac1  =((macad>>(8*5))&0xFF);// 0x00; //pc7060
	mac_conf_regs->mac.mac_dest_mac2  =((macad>>(8*4))&0xFF);// 0x19; //pc7060
	mac_conf_regs->mac.mac_dest_mac3  =((macad>>(8*3))&0xFF);// 0x99; //pc7060
	mac_conf_regs->mac.mac_dest_mac4  =((macad>>(8*2))&0xFF);// 0x24; //pc7060
	mac_conf_regs->mac.mac_dest_mac5  =((macad>>(8*1))&0xFF);// 0xEB; //pc7060
	mac_conf_regs->mac.mac_dest_mac6  =((macad>>(8*0))&0xFF);// 0xEE; //pc7060

	/*
  mac_conf_regs->mac.mac_src_mac1   = 0x00;     
  mac_conf_regs->mac.mac_src_mac2   = 0xAA;     
  mac_conf_regs->mac.mac_src_mac3   = 0xBB;     
  mac_conf_regs->mac.mac_src_mac4   = 0xCC;     
  mac_conf_regs->mac.mac_src_mac5   = 0xDD;     
  mac_conf_regs->mac.mac_src_mac6   = 0xEE;     
	 */
	mac_conf_regs->mac.mac_src_mac1  =((detectormacad>>(8*5))&0xFF);
	mac_conf_regs->mac.mac_src_mac2  =((detectormacad>>(8*4))&0xFF);
	mac_conf_regs->mac.mac_src_mac3  =((detectormacad>>(8*3))&0xFF);
	mac_conf_regs->mac.mac_src_mac4  =((detectormacad>>(8*2))&0xFF);
	mac_conf_regs->mac.mac_src_mac5  =((detectormacad>>(8*1))&0xFF);
	mac_conf_regs->mac.mac_src_mac6  =((detectormacad>>(8*0))&0xFF);
	mac_conf_regs->mac.mac_ether_type   = 0x0800;   //ipv4



	mac_conf_regs->ip.ip_ver            = 0x4;
	mac_conf_regs->ip.ip_ihl            = 0x5;
	mac_conf_regs->ip.ip_tos            = 0x0;
	mac_conf_regs->ip.ip_len            = ipPacketSize;//0x0522;   // was 0x0526;
	mac_conf_regs->ip.ip_ident          = 0x0000;
	mac_conf_regs->ip.ip_flag           = 0x2;
	mac_conf_regs->ip.ip_offset         = 0x00;
	mac_conf_regs->ip.ip_ttl            = 0x70;
	mac_conf_regs->ip.ip_protocol       = 0x11;
	mac_conf_regs->ip.ip_chksum         = 0x0000 ; //6E42 now is automatically computed
	mac_conf_regs->ip.ip_sourceip       = detipad; //0x8181CA2E;129.129.202.46
	mac_conf_regs->ip.ip_destip         = ipad; //CA57

	//#ifdef VERBOSE
	printf("mac_dest:%llx %x:%x:%x:%x:%x:%x\n",
			macad,
			mac_conf_regs->mac.mac_dest_mac1,
			mac_conf_regs->mac.mac_dest_mac2,
			mac_conf_regs->mac.mac_dest_mac3,
			mac_conf_regs->mac.mac_dest_mac4,
			mac_conf_regs->mac.mac_dest_mac5,
			mac_conf_regs->mac.mac_dest_mac6);
	printf("mac_src:%llx %x:%x:%x:%x:%x:%x\n",
			detectormacad,
			mac_conf_regs->mac.mac_src_mac1,
			mac_conf_regs->mac.mac_src_mac2,
			mac_conf_regs->mac.mac_src_mac3,
			mac_conf_regs->mac.mac_src_mac4,
			mac_conf_regs->mac.mac_src_mac5,
			mac_conf_regs->mac.mac_src_mac6);
	printf("ip_ttl:%x\n",mac_conf_regs->ip.ip_ttl);
	printf("det_ip: %x %x\n",detipad,	mac_conf_regs->ip.ip_sourceip);
	printf("dest_ip: %x %x\n",ipad, mac_conf_regs->ip.ip_destip);

	//#endif

	//checksum
	count=sizeof(mac_conf_regs->ip);
	addr=&(mac_conf_regs->ip);
	while( count > 1 )  {
		sum += *addr++;
		count -= 2;
	}
	if( count > 0 )  sum += *addr;                     // Add left-over byte, if any
	while (sum>>16) sum = (sum & 0xffff) + (sum >> 16);// Fold 32-bit sum to 16 bits
	checksum = (~sum)&0xffff;
	mac_conf_regs->ip.ip_chksum   =  checksum;
	//#ifdef VERBOSE
	printf("IP header checksum is 0x%x s\n",(unsigned int)(checksum));
	//#endif

	mac_conf_regs->udp.udp_srcport      = 0xE185;
	mac_conf_regs->udp.udp_destport     = udpport;//0xC351;
	mac_conf_regs->udp.udp_len          = udpPacketSize;//0x050E;    //was  0x0512;
	mac_conf_regs->udp.udp_chksum       = 0x0000;

#ifdef VERBOSE
	printf("Configuring TSE\n");
#endif
	tse_conf_regs->rev                 = 0xA00;
	tse_conf_regs->scratch             = 0xCCCCCCCC;
	tse_conf_regs->command_config      = 0xB;
	tse_conf_regs->mac_0               = 0x17231C00;
	tse_conf_regs->mac_1               = 0xCB4A;
	tse_conf_regs->frm_length          = 0x5DC;      //max frame length (1500 bytes) (was 0x41C)
	tse_conf_regs->pause_quant         = 0x0;
	tse_conf_regs->rx_section_empty    = 0x7F0;
	tse_conf_regs->rx_section_full     = 0x10;
	tse_conf_regs->tx_section_empty    = 0x3F8;      //was 0x7F0;
	tse_conf_regs->tx_section_full     = 0x16;
	tse_conf_regs->rx_almost_empty     = 0x8;
	tse_conf_regs->rx_almost_full      = 0x8;
	tse_conf_regs->tx_almost_empty     = 0x8;
	tse_conf_regs->tx_almost_full      = 0x3;
	tse_conf_regs->mdio_addr0          = 0x12;
	tse_conf_regs->mdio_addr1          = 0x0;

	mac_conf_regs->cdone               = 0xFFFFFFFF;


	if(ival)
		bus_w(addrr,(INT_RSTN_BIT|ENET_RESETN_BIT|WRITE_BACK_BIT|DIGITAL_TEST_BIT)); //0x2840,write shadow regs..
	else
		bus_w(addrr,(INT_RSTN_BIT|ENET_RESETN_BIT|WRITE_BACK_BIT)); //0x2840,write shadow regs..

	val=bus_r(addrr);
#ifdef VERBOSE
	printf("Value read from Multi-purpose Reg:%x\n",val);
#endif 
	//  if(val!=0x2840) return -1;

	usleep(100000);

	if(ival)
		bus_w(addrr,(INT_RSTN_BIT|ENET_RESETN_BIT|SW1_BIT|DIGITAL_TEST_BIT)); //0x2820,write shadow regs..
	else
		bus_w(addrr,(INT_RSTN_BIT|ENET_RESETN_BIT|SW1_BIT)); //0x2820,write shadow regs..

	val=bus_r(addrr);
#ifdef VERBOSE
	printf("Value read from Multi-purpose Reg:%x\n",val);
#endif 
	//  if(val!=0x2820) return -1;


	

	return adcConfigured;
}


int getAdcConfigured(){
	return adcConfigured;
}

u_int32_t runBusy(void) {
	u_int32_t s = bus_r(STATUS_REG) & 1;
#ifdef VERBOSE
	printf("status %04x\n",s);
#endif
  return s;
}

u_int32_t dataPresent(void) {
  return bus_r(LOOK_AT_ME_REG);
}

u_int32_t runState(void) {
  int s=bus_r(STATUS_REG);
#ifdef SHAREDMEMORY
  if (s&RUN_BUSY_BIT)
    write_status_sm("Running");
  else
    write_status_sm("Stopped");
#endif
#ifdef VERBOSE
  printf("status %04x\n",s);
#endif

/*  if (s==0x62001)
    exit(-1);*/
  return s;
}


// State Machine 

int startStateMachine(){
int i;
//#ifdef VERBOSE
  printf("*******Starting State Machine*******\n");
//#endif

//NEEDED?
//	cleanFifo();



  // fifoReset();
  now_ptr=(char*)ram_values;
#ifdef SHAREDMEMORY
  write_stop_sm(0);
  write_status_sm("Started");
#endif


  for(i=0;i<100;i++){
	  //start state machine
    bus_w16(CONTROL_REG, FIFO_RESET_BIT);
	  bus_w16(CONTROL_REG, 0x0);
	  bus_w16(CONTROL_REG, START_ACQ_BIT |  START_EXPOSURE_BIT);
	  bus_w16(CONTROL_REG, 0x0);
	  //verify
	  if(bus_r(STATUS_REG) & RUN_BUSY_BIT)
		  break;
	  else
		  usleep(5000);
  }
   if(i!=0)
	   printf("tried to start state machine %d times\n",i);
   if(i==100){
	   printf("\n***********COULD NOT START STATE MACHINE***************\n");
	   return FAIL;
   }

   printf("statusreg=%08x\n",bus_r(STATUS_REG));
  return OK;
}




int stopStateMachine(){
	int i;
//#ifdef VERBOSE
  printf("*******Stopping State Machine*******\n");
//#endif
#ifdef SHAREDMEMORY
  write_stop_sm(1);
  write_status_sm("Stopped");
#endif
  for(i=0;i<100;i++){
  	  //stop state machine
	  bus_w16(CONTROL_REG, STOP_ACQ_BIT);
	  bus_w16(CONTROL_REG, 0x0);
	  usleep(5000);
	  //verify
	  if(!(bus_r(STATUS_REG)&RUNMACHINE_BUSY_BIT))
		break;
  }
  if(i!=0)
	  printf("tried to stop state machine %d times\n",i);
  if(i==100){
	  printf("\n***********COULD NOT STOP STATE MACHINE***************\n");
	  return FAIL;
  }

/*
 usleep(5000);
 // if (!runBusy())
  if(!(bus_r(STATUS_REG)&RUNMACHINE_BUSY_BIT))
    return OK;
  else
    return FAIL;
    */
  printf("statusreg=%08x\n",bus_r(STATUS_REG));
  return OK;
}


int startReadOut(){
  u_int32_t status;
#ifdef VERBOSE
  printf("Starting State Machine Readout\n");
#endif
  status=bus_r(STATUS_REG)&RUN_BUSY_BIT;
#ifdef DEBUG
  printf("State machine status is %08x\n",bus_r(STATUS_REG));
#endif
  bus_w16(CONTROL_REG,  START_ACQ_BIT |START_READOUT_BIT);   //  start readout
  bus_w16(CONTROL_REG,  0x0);
  return OK;
}


// fifo routines 

u_int32_t fifoReset(void) {
	return -1;
}


u_int32_t setNBits(u_int32_t n) {
	return -1;
}

u_int32_t getNBits(){
	return -1;
}


u_int32_t fifoReadCounter(int fifonum){
	return -1;
}

u_int32_t  fifoReadStatus()
{
  // reads from the global status register

   return bus_r(STATUS_REG)&(SOME_FIFO_FULL_BIT | ALL_FIFO_EMPTY_BIT);
}

u_int32_t  fifo_full(void)
{
  // checks fifo empty flag returns 1 if fifo is empty
  //   otherwise 0
  return bus_r(STATUS_REG)&SOME_FIFO_FULL_BIT;
}


u_int16_t* fifo_read_event(int ns)
{
  int i=0;


/* #ifdef TIMEDBG  */
/*   gettimeofday(&tse,NULL); */
/* #endif    */
  if (ns==0) {
  volatile u_int32_t t = bus_r16(LOOK_AT_ME_REG);

    bus_w(DUMMY_REG,0);
    while(t!=0xffffffff) {
      if (runBusy()==0) {
	t = bus_r(LOOK_AT_ME_REG);
	if (t!=0xffffffff) {
	printf("no frame found and acquisition finished - exiting\n");
	printf("%08x %08x\n", runState(), bus_r(LOOK_AT_ME_REG));
	return NULL;
	} else { 
	  //	printf("status idle, look at me %x status %x\n", bus_r(LOOK_AT_ME_REG),runState()); 
	  break; 
	}
      }
      t = bus_r(LOOK_AT_ME_REG);
#ifdef VERBOSE
      printf(".");
#endif
    }
/* #ifdef TIMEDBG  */
/*     //    tsss=tss; */
/*     gettimeofday(&tss,NULL); */
/*     printf("look for data  = %ld usec\n", (tss.tv_usec) - (tse.tv_usec));  */
   
/*   #endif  */

  }
   //  printf("%08x %08x\n", runState(), bus_r(LOOK_AT_ME_REG));
/*   dma_memcpy(now_ptr,values ,dataBytes); */
/* #else */
  
  bus_w16(DUMMY_REG,1<<8); // read strobe to all fifos
  bus_w16(DUMMY_REG,0);
   for (i=0; i<32; i++) { 
     bus_w16(DUMMY_REG,i);
     //    dum=((u_int16_t*)(now_ptr))+i;
     // *dum=bus_r16(FIFO_DATA_REG);
     //dum=;
      *(((u_int16_t*)(now_ptr))+i)=bus_r16(FIFO_DATA_REG);
   }
/* #ifdef TIMEDBG  */
  
/*   gettimeofday(&tss,NULL); */
/*   printf("read data loop  = %ld usec\n",(tss.tv_usec) - (tse.tv_usec));  */
   
/* #endif  */
#ifdef VERBOSE
  printf("*");
#endif
  return ram_values;
}



u_int16_t* fifo_read_frame()
{
#ifdef TIMEDBG 
  gettimeofday(&tsss,NULL);
#endif   

  // u_int16_t *dum;
  int ns=0;
  now_ptr=(char*)ram_values;
  while(ns<nSamples && fifo_read_event(ns)) {
      now_ptr+=dataBytes;
      ns++;
  }
#ifdef TIMEDBG 
  // usleep(10);
  gettimeofday(&tss,NULL);
  printf("total read data loop  = %ld usec\n",(tss.tv_usec) - (tsss.tv_usec)); 
   
#endif 
  //  printf("%x %d\n",dum, ns);
  if (ns) return ram_values;
#ifdef VERBOSE
  printf("+\n");
#else
  printf("+");
#endif
  return NULL;
}


















u_int32_t* decode_data(int *datain)
{
  u_int32_t *dataout;
 // const char one=1;
  const int bytesize=8;
  char *ptr=(char*)datain;
  //int nbits=dynamicRange;
  int  ipos=0, ichan=0;;
  //int nch, boff=0;
  int ibyte;//, ibit;
  char iptr;

#ifdef VERBOSE
  printf("Decoding data for DR %d\n",dynamicRange);
#endif
  dataout=malloc(nChans*nChips*nModX*4);
  ichan=0;
  switch (dynamicRange) {
  case 1:
    for (ibyte=0; ibyte<dataBytes; ibyte++) {
      iptr=ptr[ibyte];
      for (ipos=0; ipos<bytesize; ipos++) {
	dataout[ichan]=(iptr>>(ipos))&0x1;
	ichan++;
      }
    }
    break;
  case 4:
    for (ibyte=0; ibyte<dataBytes; ibyte++) {
      iptr=ptr[ibyte]&0xff;
      for (ipos=0; ipos<2; ipos++) {
	dataout[ichan]=(iptr>>(ipos*4))&0xf;
	ichan++;
      }
    }
    break;
  case 8:
    for (ichan=0; ichan<dataBytes; ichan++) {
      dataout[ichan]=ptr[ichan]&0xff;
    }
    break;
  case 16:
    for (ichan=0; ichan<nChans*nChips*nModX; ichan++) {
      dataout[ichan]=0;
      for (ibyte=0; ibyte<2; ibyte++) {
	iptr=ptr[ichan*2+ibyte];
	dataout[ichan]|=((iptr<<(ibyte*bytesize))&(0xff<<(ibyte*bytesize)));
      }
    }
    break;
  default:
    for (ichan=0; ichan<nChans*nChips*nModX; ichan++)
      dataout[ichan]=datain[ichan]&0xffffff;
    break;
  }

#ifdef VERBOSE
  printf("decoded %d  channels\n",ichan);
#endif
  return dataout;
}



int setDynamicRange(int dr) {

  if (dr%16==0 && dr>0) {
    dynamicRange=16;
    nSamples=dr/16;
    dataBytes=NMAXMOD*NCHIP*NCHAN*2;
  }
  allocateRAM();
  printf("Setting dataBytes to %d: dr %d; samples %d\n",dataBytes, dynamicRange, nSamples);
  return   getDynamicRange();
}






int getDynamicRange() {
  //	dynamicRange=16;
  return dynamicRange*nSamples;

}

int testBus() {
  u_int32_t j;
  u_int64_t i, n, nt;
 // char cmd[100];
  u_int32_t val=0x0;
  int ifail=OK;
  // printf("%s\n",cmd);
  // system(cmd);
  i=0;

  n=1000000;
  nt=n/100;
  printf("testing bus %d times\n",(int)n);
  while (i<n) {
    // val=bus_r(FIX_PATT_REG);
    bus_w(DUMMY_REG,val);
    bus_w(FIX_PATT_REG,0);
    j=bus_r(DUMMY_REG);
    //if (i%10000==1)
    if (j!=val){
      printf("%d :  read wrong value %08x instead of %08x\n",(int)i,j, val);
      ifail++;
      //return FAIL;
    }// else
     // printf("%d : value OK 0x%08x\n",i,j);
    if ((i%nt)==0)
      printf("%lld cycles OK\n",i);
    val+=0xbbbbb;
    i++;
  }
  return ifail;
}


int setStoreInRAM(int b) {
  if (b>0)
    storeInRAM=1;
  else
    storeInRAM=0;
  return  allocateRAM();
}


int allocateRAM() {
  size_t size;


    size=dataBytes*nSamples;
  
#ifdef VERBOSE
  printf("\nnmodx=%d nmody=%d dynamicRange=%d dataBytes=%d nFrames=%d nTrains=%d, size=%d\n",nModX,nModY,dynamicRange,dataBytes,nf,nt,(int)size );
#endif

    if (size==ram_size) {

      //#ifdef VERBOSE
      printf("RAM of size %d already allocated: nothing to be done\n",(int) size);
      //#endif
      return OK;
    }



    //#ifdef VERBOSE
      printf("reallocating ram %x, size %d\n",(unsigned int)ram_values, size);
    //#endif
    //+2 was added since dma_memcpy would switch the 16 bit values and the mem is 32 bit   

      //  while (nSamples>1) {
 
      clearRAM();
     ram_values=malloc(size);
     // ram_values=realloc(ram_values,size)+2;
      // if (ram_values)
      //	break;
      // nSamples--;
      //}

  if (ram_values) {
    now_ptr=(char*)ram_values;

    //#ifdef VERBOSE
    printf("ram allocated 0x%x of size %d to %x\n",(int)now_ptr,(unsigned int) size,(unsigned int)(now_ptr+size));
    //#endif
    ram_size=size;
    return OK;
  }
  

  printf("Fatal error: there must be a memory leak somewhere! You can't allocate even one frame!\n");
  return FAIL;




}


int writeADC(int addr, int val) {


  u_int32_t valw,codata,csmask;              
  int i,cdx,ddx,j;
   cdx=0; ddx=1;
   csmask=0xfc; //  1111100
   
   codata=val + (addr<< 8);   
   printf("***** ADC SPI WRITE TO REGISTER %04X value %04X\n",addr,val);
	// start point
   valw=0xff;
   bus_w16(ADC_WRITE_REG,(valw));
   
   //chip sel bar down
   valw=((0xffffffff&(~csmask)));
   bus_w16(ADC_WRITE_REG,valw);

   for (i=0;i<24;i++) {
     //cldwn
     valw=valw&(~(0x1<<cdx));
     bus_w16(ADC_WRITE_REG,valw);
     // usleep(0);

     //write data (i)
     valw=(valw&(~(0x1<<ddx)))+(((codata>>(23-i))&0x1)<<ddx);
     bus_w16(ADC_WRITE_REG,valw);
     //  usleep(0);

     //clkup
     valw=valw+(0x1<<cdx);
     bus_w16(ADC_WRITE_REG,valw);
     // usleep(0);
   }
   
		 // stop point =start point
   valw=valw&(~(0x1<<cdx));
   // usleep(0);
   valw=0xff;
   bus_w16(ADC_WRITE_REG,(valw));
   
   //usleep in between
   //   usleep(50000);

   return OK;
}



int prepareADC(){
  printf("Preparing ADC\n");
  u_int32_t valw,codata,csmask;              
  int i,cdx,ddx,j;
   cdx=0; ddx=1;
   csmask=0x7c; //  1111100
   
    codata=0;    
    writeADC(0x08,0x3);
    writeADC(0x08,0x0);
    

          writeADC(0x16,0x01);//output clock phase
	  // writeADC(0x16,0x07);//output clock phase

     //   writeADC(0x16,0x4);//output clock phase

	  //  writeADC(0x18,0x0);// vref 1V
	    
     writeADC(0x14,0x40);//lvds reduced range -- offset binary

     writeADC(0xD,0x0);//no test mode

#ifdef TESTADC


    ////////////TEST ADC!!!!!!!!!!
    
    printf("***************************************** *******\n");
    printf("******* PUTTING ADC IN TEST MODE!!!!!!!!! *******\n");
    printf("***************************************** *******\n");
       

    //    writeADC(0xD,0x4);//ALTERNATING CHECKERBOARD

    //  writeADC(0xD,0x7);//ONE/ZERO WORD TOGGLE

/*     writeADC(0x19,0xf0);//user input */
/*     writeADC(0x1A,0xf0);//user input */
/*     writeADC(0x1B,0x0f);//user input */
/*     writeADC(0x1C,0x0f);//user input */
/*      writeADC(0xD,0x48);//user input, alternate */

/*     //writeADC(0xD,0xA);//1xsync */
//    writeADC(0xD,0xB);//1xbit high 
      writeADC(0xD,0xC);//1xmixed frequqncy 



#endif







    bus_w(ADC_LATCH_DISABLE_REG,0x0); // enable all ADCs
    bus_w(DAQ_REG,0x12); //adc pipeline=18

    bus_w(ADC_OFFSET_REG,0xbbbbbbbb);
    //   bus_w(ADC_INVERSION_REG,0x1f6170c6);

    return;
}


int clearRAM() {
  if (ram_values) {
    //#ifdef VERBOSE
    //printf("clearing RAM 0x%x\n", ram_values);
    //#endif
    free(ram_values);
    ram_values=NULL;
    now_ptr=NULL;
  }
  //#ifdef VERBOSE
  //printf("done 0x%x\n", ram_values);
  //#endif
  return OK;
}




int setMaster(int f) {

	int i;
	switch(f) {
	case NO_MASTER:
		// switch of gates or triggers
		masterMode=NO_MASTER;
		for (i=0; i<4; i++) {
			if (signals[i]==MASTER_SLAVE_SYNCHRONIZATION) {
				setFPGASignal(i,SIGNAL_OFF);
			}
		}
		break;
	case IS_MASTER:
		// configure gate or trigger out
		masterMode=IS_MASTER;
		for (i=0; i<4; i++) {
			if (signals[i]==MASTER_SLAVE_SYNCHRONIZATION) {
				switch (syncMode) {
				case NO_SYNCHRONIZATION:
					setFPGASignal(i,SIGNAL_OFF);
					break;
				case MASTER_GATES:
					setFPGASignal(i,GATE_OUT_ACTIVE_HIGH);
					break;
				case MASTER_TRIGGERS:
					setFPGASignal(i,TRIGGER_OUT_RISING_EDGE);
					break;
				case SLAVE_STARTS_WHEN_MASTER_STOPS:
					setFPGASignal(i,RO_TRIGGER_OUT_RISING_EDGE);
					break;
				default:
					;
				}
			}
		}
		break;
	case IS_SLAVE:
		// configure gate or trigger in
		masterMode=IS_SLAVE;
		for (i=0; i<4; i++) {
			if (signals[i]==MASTER_SLAVE_SYNCHRONIZATION) {
				switch (syncMode) {
				case NO_SYNCHRONIZATION:
					setFPGASignal(i,SIGNAL_OFF);
					break;
				case MASTER_GATES:
					setFPGASignal(i,GATE_IN_ACTIVE_HIGH);
					break;
				case MASTER_TRIGGERS:
					setFPGASignal(i,TRIGGER_IN_RISING_EDGE);
					break;
				case SLAVE_STARTS_WHEN_MASTER_STOPS:
					setFPGASignal(i,TRIGGER_IN_RISING_EDGE);
					break;
				default:
					;
				}
			}
		}
		break;
	default:
		//do nothing
		break;
	}

	switch(masterMode) {
	case NO_MASTER:
		return NO_MASTER;


	case IS_MASTER:
		for (i=0; i<4; i++) {
			if (signals[i]==MASTER_SLAVE_SYNCHRONIZATION) {
				switch (syncMode) {
				case NO_SYNCHRONIZATION:
					return IS_MASTER;
				case MASTER_GATES:
					if (getFPGASignal(i)==GATE_OUT_ACTIVE_HIGH)
						return IS_MASTER;
					else
						return NO_MASTER;
				case MASTER_TRIGGERS:
					if (getFPGASignal(i)==TRIGGER_OUT_RISING_EDGE)
						return IS_MASTER;
					else
						return NO_MASTER;
				case SLAVE_STARTS_WHEN_MASTER_STOPS:
					if (getFPGASignal(i)==RO_TRIGGER_OUT_RISING_EDGE)
						return IS_MASTER;
					else
						return NO_MASTER;
				default:
					return NO_MASTER;
				}

			}
		}
		break;

	case IS_SLAVE:
		for (i=0; i<4; i++) {
			if (signals[i]==MASTER_SLAVE_SYNCHRONIZATION) {
				switch (syncMode) {
				case NO_SYNCHRONIZATION:
					return IS_SLAVE;
				case MASTER_GATES:
					if (getFPGASignal(i)==GATE_IN_ACTIVE_HIGH)
						return IS_SLAVE;
					else
						return NO_MASTER;
				case MASTER_TRIGGERS:
				case SLAVE_STARTS_WHEN_MASTER_STOPS:
					if (getFPGASignal(i)==TRIGGER_IN_RISING_EDGE)
						return IS_SLAVE;
					else
						return NO_MASTER;
				default:
					return NO_MASTER;
				}
			}
		}
		break;
	}
	return masterMode;
}



int setSynchronization(int s) {

	int i;

	switch(s) {
	case NO_SYNCHRONIZATION:
		syncMode=NO_SYNCHRONIZATION;
		for (i=0; i<4; i++) {
			if (signals[i]==MASTER_SLAVE_SYNCHRONIZATION) {
				setFPGASignal(i,SIGNAL_OFF);
			}
		}
		break;
		// disable external signals?
	case MASTER_GATES:
		// configure gate in or out
		syncMode=MASTER_GATES;
		for (i=0; i<4; i++) {
			if (signals[i]==MASTER_SLAVE_SYNCHRONIZATION) {
				if (masterMode==IS_MASTER)
					setFPGASignal(i,GATE_OUT_ACTIVE_HIGH);
				else if (masterMode==IS_SLAVE)
					setFPGASignal(i,GATE_IN_ACTIVE_HIGH);
			}
		}

		break;
	case MASTER_TRIGGERS:
		// configure trigger in or out
		syncMode=MASTER_TRIGGERS;
		for (i=0; i<4; i++) {
			if (signals[i]==MASTER_SLAVE_SYNCHRONIZATION) {
				if (masterMode==IS_MASTER)
					setFPGASignal(i,TRIGGER_OUT_RISING_EDGE);
				else if (masterMode==IS_SLAVE)
					setFPGASignal(i,TRIGGER_IN_RISING_EDGE);
			}
		}
		break;


	case SLAVE_STARTS_WHEN_MASTER_STOPS:
		// configure trigger in or out
		syncMode=SLAVE_STARTS_WHEN_MASTER_STOPS;
		for (i=0; i<4; i++) {
			if (signals[i]==MASTER_SLAVE_SYNCHRONIZATION) {
				if (masterMode==IS_MASTER)
					setFPGASignal(i,RO_TRIGGER_OUT_RISING_EDGE);
				else if (masterMode==IS_SLAVE)
					setFPGASignal(i,TRIGGER_IN_RISING_EDGE);
			}
		}
		break;


	default:
		//do nothing
		break;
	}

	switch (syncMode) {

	case NO_SYNCHRONIZATION:
		return NO_SYNCHRONIZATION;

	case MASTER_GATES:

		for (i=0; i<4; i++) {
			if (signals[i]==MASTER_SLAVE_SYNCHRONIZATION) {
				if (masterMode==IS_MASTER && getFPGASignal(i)==GATE_OUT_ACTIVE_HIGH)
					return MASTER_GATES;
				else if (masterMode==IS_SLAVE && getFPGASignal(i)==GATE_IN_ACTIVE_HIGH)
					return MASTER_GATES;
			}
		}
		return NO_SYNCHRONIZATION;

	case MASTER_TRIGGERS:
		for (i=0; i<4; i++) {
			if (signals[i]==MASTER_SLAVE_SYNCHRONIZATION) {
				if (masterMode==IS_MASTER && getFPGASignal(i)==TRIGGER_OUT_RISING_EDGE)
					return MASTER_TRIGGERS;
				else if (masterMode==IS_SLAVE && getFPGASignal(i)==TRIGGER_IN_RISING_EDGE)
					return MASTER_TRIGGERS;
			}
		}
		return NO_SYNCHRONIZATION;

	case SLAVE_STARTS_WHEN_MASTER_STOPS:
		for (i=0; i<4; i++) {
			if (signals[i]==MASTER_SLAVE_SYNCHRONIZATION) {
				if (masterMode==IS_MASTER &&	getFPGASignal(i)==RO_TRIGGER_OUT_RISING_EDGE)
					return SLAVE_STARTS_WHEN_MASTER_STOPS;
				else if (masterMode==IS_SLAVE && getFPGASignal(i)==TRIGGER_IN_RISING_EDGE)
					return SLAVE_STARTS_WHEN_MASTER_STOPS;
			}
		}
		return NO_SYNCHRONIZATION;

	default:
		return NO_SYNCHRONIZATION;

	}
	return NO_SYNCHRONIZATION;

}



int readCounterBlock(int startACQ, short int CounterVals[]){

	//char *counterVals=NULL;
	//counterVals=realloc(counterVals,dataBytes);

	u_int32_t val;
	volatile u_int16_t *ptr;

	u_int32_t address = COUNTER_MEMORY_REG;
	ptr=(u_int16_t*)(CSP0BASE+address*2);


	if (runBusy()) {
		if(stopStateMachine()==FAIL)
			return FAIL;
		//waiting for the last frame read to be done
		while(runBusy())  usleep(500);
#ifdef VERBOSE
		printf("State machine stopped\n");
#endif
	}

		val=bus_r(MULTI_PURPOSE_REG);
#ifdef VERBOSE
		printf("Value of multipurpose reg:%d\n",bus_r(MULTI_PURPOSE_REG));
#endif

		memcpy(CounterVals,ptr,dataBytes);
#ifdef VERBOSE
		int i;
		printf("Copied counter memory block with size of %d bytes..\n",dataBytes);
		for(i=0;i<6;i++)
			printf("%d: %d\t",i,CounterVals[i]);
#endif


		bus_w(MULTI_PURPOSE_REG,(val&~RESET_COUNTER_BIT));
#ifdef VERBOSE
		printf("\nClearing bit 2 of multipurpose reg:%d\n",bus_r(MULTI_PURPOSE_REG));
#endif

		if(startACQ==1){
			startStateMachine();
			if(runBusy())
				printf("State machine RUNNING\n");
			else
				printf("State machine IDLE\n");
		}

/*		if(sizeof(CounterVals)<=0){
			printf("ERROR:size of counterVals=%d\n",(int)sizeof(CounterVals));
			return FAIL;
		}*/


	return OK;
}




int resetCounterBlock(int startACQ){

	char *counterVals=NULL;
	counterVals=realloc(counterVals,dataBytes);

	int ret = OK;
	u_int32_t val;
	volatile u_int16_t *ptr;


	u_int32_t address = COUNTER_MEMORY_REG;
	ptr=(u_int16_t*)(CSP0BASE+address*2);


	if (runBusy()) {
		if(stopStateMachine()==FAIL)
			return FAIL;
		//waiting for the last frame read to be done
		while(runBusy())  usleep(500);
#ifdef VERBOSE
		printf("State machine stopped\n");
#endif
	}

		val=bus_r(MULTI_PURPOSE_REG);
#ifdef VERBOSE
		printf("Value of multipurpose reg:%d\n",bus_r(MULTI_PURPOSE_REG));
#endif


		bus_w(MULTI_PURPOSE_REG,(val|RESET_COUNTER_BIT));
#ifdef VERBOSE
		printf("Setting bit 2 of multipurpose reg:%d\n",bus_r(MULTI_PURPOSE_REG));
#endif


		memcpy(counterVals,ptr,dataBytes);
#ifdef VERBOSE
		int i;
		printf("Copied counter memory block with size of %d bytes..\n",(int)sizeof(counterVals));
		for(i=0;i<6;i=i+2)
			printf("%d: %d\t",i,*(counterVals+i));
#endif


		bus_w(MULTI_PURPOSE_REG,(val&~RESET_COUNTER_BIT));
#ifdef VERBOSE
		printf("\nClearing bit 2 of multipurpose reg:%d\n",bus_r(MULTI_PURPOSE_REG));
#endif

		if(startACQ==1){
			startStateMachine();
			if(runBusy())
				printf("State machine RUNNING\n");
			else
				printf("State machine IDLE\n");
		}

		if(sizeof(counterVals)<=0){
			printf("ERROR:size of counterVals=%d\n",(int)sizeof(counterVals));
			ret = FAIL;
		}

		return ret;

	}



int calibratePedestal(int frames){
  printf("---------------------------\n");
  printf("In Calibrate Pedestal\n");
  int64_t framesBefore = getFrames();
  int64_t periodBefore = getPeriod();
  setFrames(frames);
  setPeriod(1000000);
  int dataret = OK;

  double avg[1280];
  int numberFrames = 0;

  int adc = 3;
  int adcCh = 3;
  int Ch = 3;
 

  int i = 0;
  for(i =0; i < 1280; i++){
    
    avg[i] = 0.0;
  }

  startReceiver(0);
	
  startStateMachine();

  while(dataret==OK){
    //got data
    if (fifo_read_event(0)) {
      dataret=OK;
      //sendDataOnly(file_des,&dataret,sizeof(dataret));
      //sendDataOnly(file_des,dataretval,dataBytes);
      printf("received frame\n");
	
      unsigned short *frame = (unsigned short *)now_ptr;

      int a;
      for (a=0;a<1280; a++){
	unsigned short v = (frame[a] << 8) + (frame[a] >> 8);
	//	  printf("%i: %i %i\n",a, frame[a],v);
	avg[a] += ((double)frame[a])/(double)frames;
	//if(frame[a] == 8191)
	//  printf("ch %i: %u\n",a,frame[a]);
      }
      //      printf("********\n");
      numberFrames++;
    }  

    //no more data or no data
    else {
      if(getFrames()>-2) {
	dataret=FAIL;
	printf("no data and run stopped: %d frames left\n",(int)(getFrames()+2));
	     
      } else {
	dataret=FINISHED;
	printf("acquisition successfully finished\n");

      }
      printf("dataret %d\n",dataret);
    }
  }

  

  double nf = (double)numberFrames;
  for(i =0; i < 1280; i++){
    adc = i / 256;
    adcCh = (i - adc * 256) / 32;
    Ch = i - adc * 256 - adcCh * 32;
    adc--;
    double v2 = avg[i];
    avg[i] = avg[i]/ ((double)numberFrames/(double)frames);
    unsigned short v = (unsigned short)avg[i];
    printf("setting avg for channel %i(%i,%i,%i): %i (double= %f (%f))\t", i,adc,adcCh,Ch, v,avg[i],v2);
    v=i*100;
    ram_w16(DARK_IMAGE_REG,adc,adcCh,Ch,v-4096);
    if(ram_r16(DARK_IMAGE_REG,adc,adcCh,Ch) !=  v-4096){
        printf("value is wrong (%i,%i,%i): %i \n",adc,adcCh,Ch,  ram_r16(DARK_IMAGE_REG,adc,adcCh,Ch));
    }
  }

      /*for(adc = 1; adc < 5; adc++){
    for(adcCh = 0; adcCh < 8; adcCh++){
      for(Ch=0 ; Ch < 32; Ch++){
	int channel = (adc+1) * 32 * 8  + adcCh * 32 + Ch;
	double v2 = avg[channel];
	avg[channel] = avg[channel]/ ((double)numberFrames/(double)frames);
	unsigned short v = (unsigned short)avg[channel];
	printf("setting avg for channel %i: %i (double= %f (%f))\t", channel, v,avg[channel],v2);
	ram_w16(DARK_IMAGE_REG,adc,adcCh,Ch,v-4096);
	if(ram_r16(DARK_IMAGE_REG,adc,adcCh,Ch) !=  v-4096){
	  printf("value is wrong (%i,%i,%i): %i \n",adc,adcCh,Ch,  ram_r16(DARK_IMAGE_REG,adc,adcCh,Ch));
	}
      }
    }
    }*/



  printf("frames: %i\n",numberFrames);	
  printf("corrected avg by: %f\n",(double)numberFrames/(double)frames);
  
  printf("restoring previous condition\n");
  setFrames(framesBefore);
  setPeriod(periodBefore); 
  
  printf("---------------------------\n");
  return 0;
}
uint64_t readPatternWord(int addr) {
  uint64_t word=0;
  int cntrl=0;

  if (addr>=MAX_PATTERN_LENGTH)
    return -1;
 
  
  printf("read %x\n",addr);
    cntrl= (addr&APATTERN_MASK) << PATTERN_CTRL_ADDR_OFFSET;
    bus_w(PATTERN_CNTRL_REG, cntrl);
    usleep(1000);
    bus_w(PATTERN_CNTRL_REG, cntrl | (1<< PATTERN_CTRL_READ_BIT)  );
    usleep(1000);
    printf("reading\n");
    word=get64BitReg(PATTERN_OUT_LSB_REG,PATTERN_OUT_MSB_REG);
    printf("read %llx\n", word);
    usleep(1000);
    bus_w(PATTERN_CNTRL_REG, cntrl);
    printf("done\n");
    
    return word;
}

uint64_t writePatternWord(int addr, uint64_t word) {
  

  int cntrl=0;
  if (addr>=MAX_PATTERN_LENGTH)
    return -1;

  printf("write %x %llx\n",addr, word);
  if (word!=-1){
    
    set64BitReg(word,PATTERN_IN_REG_LSB,PATTERN_IN_REG_MSB);

  
    cntrl= (addr&APATTERN_MASK) << PATTERN_CTRL_ADDR_OFFSET;
    bus_w(PATTERN_CNTRL_REG, cntrl);
    usleep(1000);
    bus_w(PATTERN_CNTRL_REG, cntrl | (1<< PATTERN_CTRL_WRITE_BIT)  );
    usleep(1000);
    bus_w(PATTERN_CNTRL_REG, cntrl);
    return word;
  } else
  return readPatternWord(addr);
}
uint64_t writePatternIOControl(uint64_t word) {
  if (word!=0xffffffffffffffff) {
    //    printf("%llx %llx %lld",get64BitReg(PATTERN_IOCTRL_REG_LSB,PATTERN_IOCTRL_REG_MSB),word);
    set64BitReg(word,PATTERN_IOCTRL_REG_LSB,PATTERN_IOCTRL_REG_MSB);
    //  printf("************ write IOCTRL (%x)\n",PATTERN_IOCTRL_REG_MSB);
  }
  return get64BitReg(PATTERN_IOCTRL_REG_LSB,PATTERN_IOCTRL_REG_MSB);
    
}
uint64_t writePatternClkControl(uint64_t word) {
  if (word!=0xffffffffffffffff) set64BitReg(word,PATTERN_IOCLKCTRL_REG_LSB,PATTERN_IOCLKCTRL_REG_MSB);
  return get64BitReg(PATTERN_IOCLKCTRL_REG_LSB,PATTERN_IOCLKCTRL_REG_MSB);
    
}

int setPatternLoop(int level, int *start, int *stop, int *n) {
  int ret=OK;
  int lval=0;

  int nreg;
  int areg;

  switch (level ) {
  case 0:
    nreg=PATTERN_N_LOOP0_REG;
    areg=PATTERN_LOOP0_AREG;
    break;
  case 1:
    nreg=PATTERN_N_LOOP1_REG;
    areg=PATTERN_LOOP1_AREG;
    break;
  case 2:
    nreg=PATTERN_N_LOOP2_REG;
    areg=PATTERN_LOOP2_AREG;
    break;
  case -1:
    nreg=-1;
    areg=PATTERN_LIMITS_AREG;
    break;
  default:
    return FAIL;
  }

  printf("level %d start %x stop %x nl %d\n",level, *start, *stop, *n);
  if (nreg>=0) {
    if ((*n)>=0) bus_w(nreg, *n);
    printf ("n %d\n",*n);
    *n=bus_r(nreg);
    printf ("n %d\n",*n);

  }

    printf("level %d start %x stop %x nl %d\n",level, *start, *stop, *n);
    lval=bus_r(areg);
/*     printf("l=%x\n",bus_r16(areg)); */
/*     printf("m=%x\n",bus_r16_m(areg)); */
    




    printf("lval %x\n",lval);
    if (*start==-1) *start=(lval>> ASTART_OFFSET)   & APATTERN_MASK;
    printf("start %x\n",*start);

    
    if (*stop==-1) *stop=(lval>> ASTOP_OFFSET)   & APATTERN_MASK;
    printf("stop %x\n",*stop);

    lval= ((*start & APATTERN_MASK) << ASTART_OFFSET) | ((*stop & APATTERN_MASK) << ASTOP_OFFSET);
    printf("lval %x\n",lval);

    bus_w(areg,lval);
    printf("lval %x\n",lval);


  return ret;
}


int setPatternWaitAddress(int level, int addr) { 
  int ret=-1;
  int reg;

  switch (level) {
  case 0:
    reg=PATTERN_WAIT0_AREG;
    break;
  case 1:
    reg=PATTERN_WAIT1_AREG;
    break;
  case 2:
    reg=PATTERN_WAIT2_AREG;
    break;
  default:
    return -1;
  };
  //  printf("BEFORE *********PATTERN IOCTRL IS %llx (%x)\n",writePatternIOControl(-1), PATTERN_IOCTRL_REG_MSB);

  // printf ("%d addr %x (%x)\n",level,addr,reg);
  if (addr>=0) bus_w(reg, addr);
  // printf ("%d addr %x %x (%x) \n",level,addr, bus_r(reg), reg);

  // printf("AFTER *********PATTERN IOCTRL IS %llx (%x)\n",writePatternIOControl(-1), PATTERN_IOCTRL_REG_MSB);

  return bus_r(reg);
}


uint64_t setPatternWaitTime(int level, uint64_t t) { 
  uint64_t ret=-1;
  int reglsb;
  int regmsb;


  switch (level) {
  case 0:
    reglsb=PATTERN_WAIT0_TIME_REG_LSB;
    regmsb=PATTERN_WAIT0_TIME_REG_MSB;
    break;
  case 1:
    reglsb=PATTERN_WAIT1_TIME_REG_LSB;
    regmsb=PATTERN_WAIT1_TIME_REG_MSB;
    break;
  case 2:
    reglsb=PATTERN_WAIT2_TIME_REG_LSB;
    regmsb=PATTERN_WAIT2_TIME_REG_MSB;
    break;
  default:
    return -1;
  }


    if (t>=0) set64BitReg(t,reglsb,regmsb);
    return get64BitReg(reglsb,regmsb);

}


void initDac(int dacnum) {


  u_int32_t offw,codata;
  u_int16_t valw; 
  int iru,i,ddx,csdx,cdx;
 
    

  //setting int reference 
  offw=DAC_REG;
   
 
    ddx=0; cdx=1;
    csdx=dacnum/8+2; 


    printf("data bit=%d, clkbit=%d, csbit=%d",ddx,cdx,csdx);
    codata=((((0x6)<<4)+((0xf))<<16)+((0x0<<4)&0xfff0));  
  
      valw=0xffff; bus_w(offw,(valw)); // start point
      valw=((valw&(~(0x1<<csdx))));bus_w(offw,valw); //chip sel bar down
      for (i=1;i<25;i++) {

	valw=(valw&(~(0x1<<cdx)));bus_w(offw,valw); //cldwn	
	valw=((valw&(~(0x1<<ddx)))+(((codata>>(24-i))&0x1)<<ddx));bus_w(offw,valw);//write data (i)
	//	printf("%d ", ((codata>>(24-i))&0x1));
        

	valw=((valw&(~(0x1<<cdx)))+(0x1<<cdx));bus_w(offw,valw);//clkup
      }
      // printf("\n ");
     
	
       	valw=((valw&(~(0x1<<csdx)))+(0x1<<csdx));bus_w(offw,valw); //csup

	valw=(valw&(~(0x1<<cdx)));bus_w(offw,valw); //cldwn




      valw=0xffff; bus_w(offw,(valw)); // stop point =start point of course */


 //end of setting int reference 







}

int setDacRegister(int dacnum,int dacvalue) {
    int val;
    if (dacvalue==-100)
      dacvalue=0xffff;


  if (dacnum%2) {
    val=((dacvalue & 0xffff)<<16) | getDacRegister(dacnum-1);
  }  else {
    val=(getDacRegister(dacnum+1)<<16) | (dacvalue & 0xffff);
    
  }
  
  printf("Dac register %x wrote %08x\n",(DAC_REG_OFF+dacnum/2)<<11,val);
    bus_w((DAC_REG_OFF+dacnum/2)<<11, val);

  return getDacRegister(dacnum);
  

}
int getDacRegister(int dacnum) {

  int retval;

  retval=bus_r((DAC_REG_OFF+dacnum/2)<<11);
  printf("Dac register %x read %08x\n",(DAC_REG_OFF+dacnum/2)<<11,retval);
  if (dacnum%2) 
    return (retval>>16)&0xffff;
  else
    return retval&0xffff;
  
}


int setDac(int dacnum,int dacvalue){ 

  u_int32_t offw,codata;
  u_int16_t valw; 
  int iru,i,ddx,csdx,cdx;
  int retval;

  int dacch=0;

  if (dacvalue>=0) {
    
  //setting int reference 
  offw=DAC_REG;
   
 
    ddx=0; cdx=1;
    csdx=dacnum/8+2; 

    dacch=dacnum%8;

    printf("data bit=%d, clkbit=%d, csbit=%d",ddx,cdx,csdx);

    //modified to power down single channels

    //   codata=((((0x2)<<4)+((dacch)&0xf))<<16)+((dacvalue<<4)&0xfff0);  
    codata=((((0x3)<<4)+((dacch)&0xf))<<16)+((dacvalue<<4)&0xfff0);  
      valw=0xffff; bus_w(offw,(valw)); // start point
      valw=((valw&(~(0x1<<csdx))));bus_w(offw,valw); //chip sel bar down
      for (i=1;i<25;i++) {

	valw=(valw&(~(0x1<<cdx)));bus_w(offw,valw); //cldwn	
	valw=((valw&(~(0x1<<ddx)))+(((codata>>(24-i))&0x1)<<ddx));bus_w(offw,valw);//write data (i)
	//	printf("%d ", ((codata>>(24-i))&0x1));
        

	valw=((valw&(~(0x1<<cdx)))+(0x1<<cdx));bus_w(offw,valw);//clkup
      }
      // printf("\n ");
     
	
       	valw=((valw&(~(0x1<<csdx)))+(0x1<<csdx));bus_w(offw,valw); //csup

	valw=(valw&(~(0x1<<cdx)));bus_w(offw,valw); //cldwn




      valw=0xffff; bus_w(offw,(valw)); // stop point =start point of course */
     
    
    printf("Writing %d in DAC(0-15) %d \n",dacvalue,dacnum);
    setDacRegister(dacnum,dacvalue); 
  } else if (dacvalue==-100) {

    printf("switching off dac %d\n", dacnum);
    
    csdx=dacnum/8+2; 

    dacch=dacnum%8;
    ddx=0; cdx=1;

    codata=((((0x4)<<4)+((dacch)&0xf))<<16)+((dacvalue<<4)&0xfff0);  
  
      valw=0xffff; bus_w(offw,(valw)); // start point
      valw=((valw&(~(0x1<<csdx))));bus_w(offw,valw); //chip sel bar down
      for (i=1;i<25;i++) {

	valw=(valw&(~(0x1<<cdx)));bus_w(offw,valw); //cldwn	
	valw=((valw&(~(0x1<<ddx)))+(((codata>>(24-i))&0x1)<<ddx));bus_w(offw,valw);//write data (i)
	//	printf("%d ", ((codata>>(24-i))&0x1));
        

	valw=((valw&(~(0x1<<cdx)))+(0x1<<cdx));bus_w(offw,valw);//clkup
      }
      // printf("\n ");
     
	
       	valw=((valw&(~(0x1<<csdx)))+(0x1<<csdx));bus_w(offw,valw); //csup

	valw=(valw&(~(0x1<<cdx)));bus_w(offw,valw); //cldwn




      valw=0xffff; bus_w(offw,(valw)); // stop point =start point of course */
     
    
    printf("Writing %d in DAC(0-15) %d \n",dacvalue,dacnum);
    setDacRegister(dacnum,dacvalue); 

  }





  return getDacRegister(dacnum); 
}



