#ifndef RECEIVER_DEFS_H
#define RECEIVER_DEFS_H

#include "sls_receiver_defs.h"

#include <stdint.h> 

#define GOODBYE 							-200

#define DO_NOTHING		0
#define CREATE_FILES	1
#define DO_EVERYTHING	2

#define BUF_SIZE        		(16*1024*1024) //16mb
#define SAMPLE_TIME_IN_NS		100000000//100ms
#define MAX_JOBS_PER_THREAD		1000
#define HEADER_SIZE_NUM_TOT_PACKETS	2
#define HEADER_SIZE_NUM_FRAMES	2
#define HEADER_SIZE_NUM_PACKETS	1


//all max frames defined in sls_receiver_defs.h.  20000 gotthard, 100000 for short gotthard, 1000 for moench, eiger 20000


#define GOTTHARD_FIFO_SIZE					25000 //cannot be less than max jobs per thread = 1000
/*#define GOTTHARD_ALIGNED_FRAME_SIZE		4096*/
#define GOTTHARD_PACKETS_PER_FRAME			2
#define GOTTHARD_ONE_PACKET_SIZE			1286
#define GOTTHARD_BUFFER_SIZE 				(GOTTHARD_ONE_PACKET_SIZE*GOTTHARD_PACKETS_PER_FRAME) 	//1286*2
#define GOTTHARD_DATA_BYTES	 				(1280*GOTTHARD_PACKETS_PER_FRAME)						//1280*2

#define GOTTHARD_FRAME_INDEX_MASK			0xFFFFFFFE
#define GOTTHARD_FRAME_INDEX_OFFSET			1
#define GOTTHARD_PACKET_INDEX_MASK			0x1

#define GOTTHARD_PIXELS_IN_ROW				1280
#define GOTTHARD_PIXELS_IN_COL				1


#define GOTTHARD_SHORT_PACKETS_PER_FRAME	1
#define GOTTHARD_SHORT_BUFFER_SIZE			518
#define GOTTHARD_SHORT_DATABYTES			512
#define GOTTHARD_SHORT_FRAME_INDEX_MASK		0xFFFFFFFF
#define GOTTHARD_SHORT_FRAME_INDEX_OFFSET	0
#define GOTTHARD_SHORT_PACKET_INDEX_MASK	0
#define GOTTHARD_SHORT_PIXELS_IN_ROW		256
#define GOTTHARD_SHORT_PIXELS_IN_COL		1




#define MOENCH_FIFO_SIZE					2500 //cannot be less than max jobs per thread = 1000
/*#define MOENCH_ALIGNED_FRAME_SIZE			65536*/
#define MOENCH_PACKETS_PER_FRAME			40
#define MOENCH_ONE_PACKET_SIZE				1286
#define MOENCH_BUFFER_SIZE 					(MOENCH_ONE_PACKET_SIZE*MOENCH_PACKETS_PER_FRAME) 	//1286*40
#define MOENCH_DATA_BYTES	 				(1280*MOENCH_PACKETS_PER_FRAME)						//1280*40

#define MOENCH_FRAME_INDEX_MASK				0xFFFFFF00
#define MOENCH_FRAME_INDEX_OFFSET			8
#define MOENCH_PACKET_INDEX_MASK			0xFF

#define MOENCH_BYTES_PER_ADC				(40*2)
#define MOENCH_PIXELS_IN_ONE_ROW			160
#define MOENCH_BYTES_IN_ONE_ROW				(MOENCH_PIXELS_IN_ONE_ROW*2)





#define EIGER_FIFO_SIZE						2500 //cannot be less than max jobs per thread = 1000
/*#define EIGER_ALIGNED_FRAME_SIZE			65536*/
#define EIGER_PACKETS_PER_FRAME				1  //default for 16B
#define EIGER_ONE_PACKET_SIZE				1040 //default for 16B
#define EIGER_BUFFER_SIZE 					(EIGER_ONE_PACKET_SIZE*EIGER_PACKETS_PER_FRAME) 	//1040*1 //default for 16B
#define EIGER_DATA_BYTES	 				(1032*EIGER_PACKETS_PER_FRAME)						//1280*40 //default for 16B

#define EIGER_FRAME_INDEX_MASK				0xFFFFFF00
#define EIGER_FRAME_INDEX_OFFSET			8
#define EIGER_PACKET_INDEX_MASK				0xFF



#endif
