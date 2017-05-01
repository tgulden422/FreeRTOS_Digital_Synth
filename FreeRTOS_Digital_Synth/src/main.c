/*************************************************************************************************
                                          --READ ME--

	This is a digital synthesizer instrument written for FreeRTOS. It receives MIDI note on/off 
	commands via UART. It outputs to a Microchip MCP4821 DAC. It has three voices. Waveform can
	be changed by MIDI program change command.

	Author: Thaddeus Gulden
	Last updated: 21 April 2017

*************************************************************************************************/

/******* HEADER INCLUDES ********/
#include <asf.h>
#include "task.h"
#include "semphr.h"


/**********  DEFINE  ************/
#define	DAC_CMD_MASK		(	0x3000	) //to logical OR with every outgoing DAC sample, for MCP4821

#define USART_BAUD_RATE		(	115200	)

#define USART_BUFF_LEN		(	10		)

#define portTICK_RATE_uS	(	(portTickType) 1000000 / configTICK_RATE_HZ	)

#define SYSTEM_CLK_FREQ		configCPU_CLOCK_HZ

#define SAMPLE_FREQ			configTICK_RATE_HZ

/********   TYPE DEFS  **********/
//voicing struct goes here

union u16_to_u8 {
	uint16_t u16;
	uint8_t u8[2];
};

enum wave_type{
	SQUARE,
	SAW,
	TRI
};

struct voices{
	bool v_enable;
	enum wave_type v_type;
	long v_counter;
	long v_period;
};

/****** FUNCTION PROTOTYPES  ****/
//clock config functions
void configure_extosc32k(void);
void configure_dfll_open_loop(void);
void configure_gclock_generator( void );
void configure_gclock_channel( void );
void dfll_setup( void );
void extosc32k_setup( void );

//UART config functions
void configure_usart(void);
void configure_usart_EDBG(void);
void configure_usart_callbacks(void);

//SPI config functions
void configure_spi_master(void);

//callbacks
void usart_read_callback(struct usart_module *const usart_module);

//FreeRTOS Tasks
static void vUARTHandlerTask( void *pvParameters );
static void vPeriodicSPITask( void *pvParameters );
static void vMIDIInterpreter( void *pvParameters );

//Application functions
void write_to_MCP4821( uint16_t input16 );
void sample_calc( void );
uint16_t fraction_of_FFF(long num, long den);


/*******   GLOBAL VARS  *********/
//SPI instances
struct spi_module spi_master_instance;
struct spi_slave_inst slave;

//UART instance
struct usart_module usart_instance;
struct usart_module usart_instance_EDBG;

//FreeRTOS Vars
xQueueHandle sampleQueue;
xQueueHandle messageQueue;

xSemaphoreHandle UARTsem;

//SPI transfer union
union u16_to_u8 SPI_union;

//UART buffer
uint8_t	UART_buffer[USART_BUFF_LEN];

//loop index
long n;
long j;

//voice state variables
static struct voices active_voices[4];
static bool full_queue_flag;
static uint16_t sample_buffer;


/***  APPLICATION FUNCTIONS  ****/
void write_to_MCP4821( uint16_t input16 )
{
	union u16_to_u8 sample_to_send;
	uint8_t sent_bytes[2];
	int i;

	sample_to_send.u16 = (input16 & 0xFFF) | (DAC_CMD_MASK);

	sent_bytes[0] = sample_to_send.u8[1];
	sent_bytes[1] = sample_to_send.u8[0];

	//send sample to spi peripheral
	spi_select_slave(&spi_master_instance, &slave, true);
	spi_write_buffer_wait(&spi_master_instance, sent_bytes, 2);
	spi_select_slave(&spi_master_instance, &slave, false);
}


void sample_calc( void )
{
	for(j=0; j<4; j++)
	{
		sample_buffer = 0;
		if(active_voices[j].v_enable)
		{
			if(active_voices[j].v_type == SQUARE)
			{
				if(active_voices[j].v_counter <= (active_voices[j].v_period)/2)
				{
					sample_buffer += (uint16_t) (0xFFF >> 2);
				}
			}

			if(active_voices[j].v_type == SAW)
			{
				sample_buffer += (uint16_t) (fraction_of_FFF(active_voices[j].v_counter, active_voices[j].v_period) >> 2);
			}

			if(active_voices[j].v_type == TRI)
			{
				if(active_voices[j].v_counter <= ((active_voices[j].v_period) >> 1))
				{
					sample_buffer += (uint16_t) (fraction_of_FFF((active_voices[j].v_counter << 1), active_voices[j].v_period) >> 2);
				}
				else if(active_voices[j].v_counter > ((active_voices[j].v_period) >> 1))
				{
					sample_buffer += (uint16_t) (fraction_of_FFF(((active_voices[j].v_period - active_voices[j].v_counter) << 1), active_voices[j].v_period) >> 2);
				}
			}

			if(active_voices[j].v_counter < active_voices[j].v_period)
			{
				active_voices[j].v_counter++;
			}
			else active_voices[j].v_counter = 0;
		}
	}
}

uint16_t fraction_of_FFF(long num, long den)
{
	float num_f = num;
	float den_f = den;
	float output = (float) 0xFFF;

	output = output * (num_f/den_f);

	return ((uint16_t) output);
}


/******  CONFIG FUNCTIONS  ******/
//clock config functions
void dfll_setup( void )
{
	#if (!SAMC21)
	/* Configure the DFLL in open loop mode using default values */
	configure_dfll_open_loop();
	/* Enable the DFLL oscillator */
	enum status_code dfll_status =
	system_clock_source_enable(SYSTEM_CLOCK_SOURCE_DFLL);
	if (dfll_status != STATUS_OK) {
		/* Error enabling the clock source */
	}
	/* Configure flash wait states before switching to high frequency clock */
	system_flash_set_waitstates(2);
	/* Change system clock to DFLL */
	struct  system_gclk_gen_config config_gclock_gen;
	system_gclk_gen_get_config_defaults(&config_gclock_gen);
	config_gclock_gen.source_clock = SYSTEM_CLOCK_SOURCE_DFLL;
	config_gclock_gen.division_factor = 1;
	system_gclk_gen_set_config(GCLK_GENERATOR_0, &config_gclock_gen);
	#endif
}

void extosc32k_setup( void )
{
	/* Configure the external 32KHz oscillator */
	configure_extosc32k();
	/* Enable the external 32KHz oscillator */
		enum status_code osc32k_status =
	system_clock_source_enable(SYSTEM_CLOCK_SOURCE_XOSC32K);
	if (osc32k_status != STATUS_OK) {
		/* Error enabling the clock source */
	}
}

void configure_extosc32k( void )
{
	struct  system_clock_source_xosc32k_config config_ext32k;
	system_clock_source_xosc32k_get_config_defaults(&config_ext32k);
	config_ext32k.startup_time = SYSTEM_XOSC32K_STARTUP_4096;
	system_clock_source_xosc32k_set_config(&config_ext32k);
}

#if (!SAMC21)
void configure_dfll_open_loop( void )
{
	struct  system_clock_source_dfll_config config_dfll;
	system_clock_source_dfll_get_config_defaults(&config_dfll);
	system_clock_source_dfll_set_config(&config_dfll);
}
#endif

void configure_gclock_generator( void )
{
	struct  system_gclk_gen_config gclock_gen_conf;
	system_gclk_gen_get_config_defaults(&gclock_gen_conf);
	#if (SAML21) || (SAML22)
	gclock_gen_conf.source_clock = SYSTEM_CLOCK_SOURCE_OSC16M;
	gclock_gen_conf.division_factor = 4;
	#elif (SAMC21)
	gclock_gen_conf.source_clock = SYSTEM_CLOCK_SOURCE_OSC48M;
	gclock_gen_conf.division_factor = 4;
	#else
	gclock_gen_conf.source_clock = SYSTEM_CLOCK_SOURCE_OSC8M;
	gclock_gen_conf.division_factor = 4;
	#endif
	system_gclk_gen_set_config(GCLK_GENERATOR_2, &gclock_gen_conf);
	system_gclk_gen_enable(GCLK_GENERATOR_2);
}

void configure_gclock_channel( void )
{
	struct  system_gclk_chan_config gclk_chan_conf;
	system_gclk_chan_get_config_defaults(&gclk_chan_conf);
	gclk_chan_conf.source_generator = GCLK_GENERATOR_2;
	#if (SAMD10) || (SAMD11)
	system_gclk_chan_set_config(TC1_GCLK_ID, &gclk_chan_conf);
	system_gclk_chan_enable(TC1_GCLK_ID);
	#else
	system_gclk_chan_set_config(TC3_GCLK_ID, &gclk_chan_conf);
	system_gclk_chan_enable(TC3_GCLK_ID);
	#endif
}

void configure_usart(void)
{
	struct usart_config config_usart;
	usart_get_config_defaults(&config_usart);
	config_usart.baudrate = USART_BAUD_RATE;
	config_usart.mux_setting = USART_RX_1_TX_0_XCK_1;
	config_usart.pinmux_pad0 = PINMUX_PA16C_SERCOM1_PAD0;
	config_usart.pinmux_pad1 = PINMUX_PA17C_SERCOM1_PAD1;
	config_usart.pinmux_pad2 = PINMUX_UNUSED;
	config_usart.pinmux_pad3 = PINMUX_UNUSED;
	config_usart.start_frame_detection_enable = true;
	config_usart.generator_source = GCLK_GENERATOR_2;
	while (usart_init(&usart_instance, SERCOM1
	, &config_usart) != STATUS_OK) {
	}
	usart_enable(&usart_instance);
}

void configure_usart_callbacks(void)
{
	usart_register_callback(&usart_instance,
	usart_read_callback, USART_CALLBACK_START_RECEIVED);
	usart_enable_callback(&usart_instance, USART_CALLBACK_START_RECEIVED);
}

void configure_usart_EDBG(void)
{
	struct usart_config config_usart;
	usart_get_config_defaults(&config_usart);
	config_usart.baudrate = USART_BAUD_RATE;
	config_usart.mux_setting = EDBG_CDC_SERCOM_MUX_SETTING;
	config_usart.pinmux_pad0 = EDBG_CDC_SERCOM_PINMUX_PAD0;
	config_usart.pinmux_pad1 = EDBG_CDC_SERCOM_PINMUX_PAD1;
	config_usart.pinmux_pad2 = EDBG_CDC_SERCOM_PINMUX_PAD2;
	config_usart.pinmux_pad3 = EDBG_CDC_SERCOM_PINMUX_PAD3;
	config_usart.generator_source = GCLK_GENERATOR_2;
	
	stdio_serial_init(&usart_instance_EDBG, EDBG_CDC_MODULE, &config_usart);

	usart_enable(&usart_instance_EDBG);
}

void configure_spi_master(void)
{
	struct spi_config config_spi_master;
	struct spi_slave_inst_config slave_dev_config;
	/* Configure and initialize software device instance of peripheral
	slave */
	spi_slave_inst_get_config_defaults(&slave_dev_config);
	slave_dev_config.ss_pin = PIN_PB08;
	spi_attach_slave(&slave, &slave_dev_config);
	/* Configure, initialize and enable SERCOM SPI module */
	spi_get_config_defaults(&config_spi_master);
	config_spi_master.mux_setting = EXT1_SPI_SERCOM_MUX_SETTING;
	/* Configure pad 0 for data in */
	config_spi_master.pinmux_pad0 = EXT1_SPI_SERCOM_PINMUX_PAD0;
	/* Configure pad 1 as unused */
	config_spi_master.pinmux_pad1 = PINMUX_UNUSED;
	/* Configure pad 2 for data out */
	config_spi_master.pinmux_pad2 = EXT1_SPI_SERCOM_PINMUX_PAD2; //PA06
	/* Configure pad 3 for SCK */
	config_spi_master.pinmux_pad3 = EXT1_SPI_SERCOM_PINMUX_PAD3; //PA07
	config_spi_master.generator_source = GCLK_GENERATOR_2;
	spi_init(&spi_master_instance, EXT1_SPI_MODULE, &config_spi_master);
	spi_enable(&spi_master_instance);
}

/*****  INTERRUPT HANDLERS  *****/
void usart_read_callback(struct usart_module *const usart_module)
{
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

	//sets semaphore for secondary handler task to receive
	xSemaphoreGiveFromISR( UARTsem, &xHigherPriorityTaskWoken );

	portEND_SWITCHING_ISR( xHigherPriorityTaskWoken );
	printf( "Interrupt - Semaphore generated.\r\n" );
}


/******  FreeRTOS TASKS   *******/
static void vUARTHandlerTask( void *pvParameters )
{	
	uint16_t temp;
	uint8_t temp8;

	//attempts to take semaphore if it is set
	//xSemaphoreTake( UARTsem, 0 );

	while(1)
	{
		xSemaphoreTake( UARTsem, portMAX_DELAY );
		
		//reads out everything in USART buffer, and stores to messageQueue
		while(usart_read_wait(&usart_instance, &temp) == STATUS_OK)
		{
			temp8 = (uint8_t) temp;	
			xQueueSendToBack(messageQueue, &temp8, 50/portTICK_RATE_uS);
		}
	}
}

static void vPeriodicSPITask( void *pvParameters )
{
	//gets current tick count
	portTickType xLastWakeTime = xTaskGetTickCount();
	portBASE_TYPE xStatus;
	uint16_t sample_to_send;

	while(1)
	{
		//pull sample from queue
		xStatus = xQueueReceive( sampleQueue, &sample_to_send, 0 );

		//send sample to DAC
		if (xStatus == pdTRUE) write_to_MCP4821( sample_to_send );

		vTaskDelayUntil( &xLastWakeTime, 50/portTICK_RATE_uS );
	}
}

static void vMIDIInterpreter( void *pvParameters )
{
	uint8_t MIDI_message;
	portBASE_TYPE xStatus;

	while(1)
	{
		//pull messages from MIDI queue and change the voice struct variables
		xStatus = xQueueReceive( messageQueue, &MIDI_message, 0 );

		if(xStatus == pdTRUE){
		//do midi interpretation here
		}
		else{
		taskYIELD();
		}
	}
}

static void vSampleCalcTask( void *pvParameters )
{
	portBASE_TYPE xStatus;
	
	while(1)
	{
		//interpret state variables and do sample computation here, then push to sample queue
		while(full_queue_flag)
		{
			xStatus = xQueueSendToBackFromISR(sampleQueue, &sample_buffer, 0);
			if(xStatus == pdTRUE) full_queue_flag = false;
		}

		sample_calc();

		xStatus = xQueueSendToBackFromISR(sampleQueue, &sample_buffer, 0);
		if (xStatus == pdFALSE)
		{
			full_queue_flag = true;
		}
	}
}

/*******      MAIN     **********/
int main ( void )
{
	system_init();


	extosc32k_setup();
	dfll_setup();	configure_gclock_generator();
	configure_gclock_channel();	configure_usart();
	configure_usart_EDBG();
	configure_usart_callbacks();	system_interrupt_enable_global();

	configure_spi_master();


	printf("PROGRAM START!\r\n");

	uint16_t test_sample = fraction_of_FFF(45, 45);

	active_voices[0].v_enable = true;
	active_voices[0].v_type = TRI;
	active_voices[0].v_counter = 0;
	active_voices[0].v_period = 5;

	j=0;

	//SQUARE TEST
	while(0)
	{
		if(active_voices[j].v_counter <= (active_voices[j].v_period)/2)
		{
			sample_buffer = (uint16_t) (0xFFF >> 2);
		}
		else sample_buffer = 0;

		if(active_voices[0].v_counter < active_voices[0].v_period)
		{
			active_voices[0].v_counter++;
		}
		else active_voices[0].v_counter = 0;
	
		write_to_MCP4821(sample_buffer);
	}


	//TRIANGLE TEST
	while(1)
	{

		if(active_voices[j].v_counter <= ((active_voices[j].v_period) >> 1))
		{
			sample_buffer = (uint16_t) (fraction_of_FFF((active_voices[j].v_counter << 1), active_voices[j].v_period) >> 2);
		}
		else if(active_voices[j].v_counter > ((active_voices[j].v_period) >> 1))
		{
			sample_buffer = (uint16_t) (fraction_of_FFF(((active_voices[j].v_period - active_voices[j].v_counter) << 1), active_voices[j].v_period) >> 2);
		}

		if(active_voices[0].v_counter < active_voices[0].v_period)
		{
			active_voices[0].v_counter++;
		}
		else active_voices[0].v_counter = 0;

		write_to_MCP4821(sample_buffer*3);
	}


	//SAW TEST
	while(1)
	{

		sample_buffer = (uint16_t) ((0xFFF * (active_voices[0].v_counter/active_voices[0].v_period)) >> 2);


		if(active_voices[0].v_counter < active_voices[0].v_period)
		{
			active_voices[0].v_counter++;
		}
		else active_voices[0].v_counter = 0;
	
		write_to_MCP4821(sample_buffer);
	}

// 	//global variable inits
// 	full_queue_flag = false;
// 
// 	for (n=0; n<4; n++)
// 	{
// 		active_voices[n].v_enable = false;
// 		active_voices[n].v_counter = 0;
// 	}
// 
// 	active_voices[0].v_enable = true;
// 	active_voices[0].v_type = SQUARE;
// 	active_voices[0].v_counter = 0;
// 	active_voices[0].v_period = 45;
// 
// 	//Begin FreeRTOS Setup
// 
// 	//create queues and semaphore
// 	sampleQueue = xQueueCreate(100, sizeof(uint16_t));
// 	messageQueue = xQueueCreate(100, sizeof(uint8_t));
// 
// 	//UARTsem = vSemaphoreCreateBinary();
// 
// 	xTaskCreate(vSampleCalcTask, "Synth", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
// 	xTaskCreate(vMIDIInterpreter, "MIDI Interp", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
// 	xTaskCreate(vPeriodicSPITask, "SPI Push", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
// 	//xTaskCreate(vUARTHandlerTask, "UART read", configMINIMAL_STACK_SIZE, NULL, 4, NULL);
// 
// 	vTaskStartScheduler();
// 	while(1);


}
