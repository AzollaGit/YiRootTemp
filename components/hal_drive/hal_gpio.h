#pragma once


//=========================================================
 #define LED_WRITE(X)	
//=========================================================================================
 
// 状态标识位
typedef struct {
	bool bit0   :  1;
	bool bit1   :  1;
	bool bit2   :  1;	
	bool bit3   :  1;
	bool bit4   :  1;
	bool bit5   :  1;
	bool bit6   :  1;
	bool bit7   :  1;	
} bit8_t;

typedef union {
	bit8_t  bits;
	uint8_t value;  
} union8_t;

typedef struct {
	bool bit0   :  1;
	bool bit1   :  1;
	bool bit2   :  1;	
	bool bit3   :  1;
	bool bit4   :  1;
	bool bit5   :  1;
	bool bit6   :  1;
	bool bit7   :  1;	
	bool bit8   :  1;
	bool bit9   :  1;
	bool bit10  :  1;	
	bool bit11  :  1;
	bool bit12  :  1;
	bool bit13  :  1;
	bool bit14  :  1;
	bool bit15  :  1;	
} bit16_t;

typedef union {
	bit16_t  bits;
	uint16_t value;  
} union16_t;

typedef union {
	uint8_t  u8[8];
	uint16_t u16[4];  
	uint32_t u32[2];  
	uint64_t u64;  
} union64_t;

void hal_gpio_init(void);


