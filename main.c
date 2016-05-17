#include <inttypes.h>
#include <msp430g2252.h>

#define DIGIT_1 BIT3 //    Common Anode LEDs         //          ^ VCC                     ^ VCC
#define DIGIT_2 BIT2 //  +--1--A--F--2--3--B--+      //          |                         |
#define DIGIT_3 BIT1 //  | 4 x 7 Segment + DP |      //           \ Button_2                \ Button_1
#define DIGIT_4 BIT0 //  +--E--D--DP-C--G--4--+      //          |                         |

//port 1
#define SEG_A BIT0
#define SEG_B BIT1
#define SEG_C BIT2
#define SEG_D BIT3
#define SEG_E BIT4
#define SEG_F BIT5
#define SEG_G BIT7

//define button for editing
//port 1
#define BUTTON_1 BIT4  //P2.4

static const uint8_t glyph_pattern[ 13 ] = {
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,         // 0
	SEG_B | SEG_C,                                         // 1
	SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,                 // 2
	SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,                 // 3
	SEG_B | SEG_C | SEG_F | SEG_G,                         // 4
	SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,                 // 5
	SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,         // 6
	SEG_A | SEG_B | SEG_C,                                 // 7
	SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, // 8
	SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,         // 9
	0,                                                     // none
	SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,         // A
	SEG_A | SEG_B | SEG_E | SEG_F | SEG_G                  // P
};
#define GLYPH_none 10
#define GLYPH_A    11
#define GLYPH_P    12

#define CORRECTION 2
static const uint8_t glyph_delay[ 13 ] = {
	6 + CORRECTION, // 0
	2 + CORRECTION, // 1
	5 + CORRECTION, // 2
	5 + CORRECTION, // 3
	4 + CORRECTION, // 4
	5 + CORRECTION, // 5
	6 + CORRECTION, // 6
	3 + CORRECTION, // 7
	7 + CORRECTION, // 8
	6 + CORRECTION, // 9
	0 + CORRECTION, // none
	7 + CORRECTION, // A
	5 + CORRECTION  // P
};

static volatile uint8_t ticks = 0, seconds = 0, minutes = 0, hours = 0;
static volatile uint8_t displaying_watchdog = 0;
static volatile uint16_t remaining = 267;

typedef volatile unsigned int bool;
#define true 1
#define false 0
bool go_to_sleep = false;
bool tick_happened = false;
bool time_is_set = false;

typedef enum  {SLEEPING,
			  TIME_DISPLAYING, TIME_HOUR_EDITING, TIME_MINUTE_EDITING, PAPA_DISPLAYING } TState;
TState state = TIME_DISPLAYING;

typedef volatile enum  {
			  BUTTON_READY, BUTTON_PRESSED, BUTTON_HOLD  } BState;
BState bState = BUTTON_READY;

static void display_digit( uint8_t digit, uint8_t glyph_index)
{
	//create negative edge to switch off digit
	P2OUT &= ~digit;

	//set glyp of digit
	P1OUT = 0xFF ^ glyph_pattern[glyph_index];

	//create positive edge to switch on digit
	P2OUT |= digit;

	uint8_t on_duration = glyph_delay[ glyph_index ];
	uint8_t i;
	for ( i = on_duration; i > 0 ; --i ) {
		__delay_cycles( 500 );//20
	}

	//create negative edge to switch off digit
    P2OUT &= ~digit;
}

static void config_button1_input()
{
	//config port 1 as input mode
	P2SEL &= ~BUTTON_1; // disable timera0 out function
	P2DIR &= ~BUTTON_1;// input
	P2OUT &= ~BUTTON_1;// select pull-up mode
	P2IES &= ~BUTTON_1;// select the positive edge (low -> high transition) to cause an interrupt
	P2IFG &= ~BUTTON_1;// clear any pending interrupt flags
	P2IE |= BUTTON_1;// enable button interrupt
}

static void config_registers()
{
	WDTCTL = WDTPW + WDTHOLD;	// stop WDT
	
	BCSCTL1 = CALBC1_1MHZ;
	DCOCTL = CALDCO_1MHZ;

	BCSCTL3 |= LFXT1S_0 | XCAP_3;  //12.5pF cap- setting for 32768Hz crystal

	WDTCTL = WDT_ADLY_16;
	IE1 |= WDTIE;

	//config push buttons
	config_button1_input();

	//config segments
	P1DIR |= SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G;
	P1OUT |= SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G;//set all strobes high

	//set digit controller
	P2DIR |= DIGIT_1 | DIGIT_2 | DIGIT_3 | DIGIT_4; // output
	P2OUT &= ~DIGIT_1 | ~DIGIT_2 | ~DIGIT_3 | ~DIGIT_4; //set all strobes low
}

int main(void){
	static volatile uint8_t digit_1,digit_2,digit_3,digit_4;

	config_registers();
	__enable_interrupt();

	while ( true ) {

		if ( go_to_sleep ) {
			go_to_sleep = false;
			state = SLEEPING;
			LPM3;
		}

		if ( tick_happened ) {
			tick_happened = false;
		}

		//handle button
		volatile int counter=0;

		//check button
		if(bState == BUTTON_PRESSED && (time_is_set == false)){
			while(counter < 0x6900)//0x6900
			{
				counter++;
			}

			if(P2IN & (1<<4)) {
				bState = BUTTON_HOLD;
			}
		}

		uint8_t hour = 0;
		uint8_t minute = 0;
		switch ( state ) {
		case SLEEPING:
			break;

		case TIME_DISPLAYING:
		case TIME_HOUR_EDITING:
		case TIME_MINUTE_EDITING:
			hour = hours;
			minute = minutes;

			if(state == TIME_DISPLAYING && bState == BUTTON_HOLD) {
					state = TIME_HOUR_EDITING;
					bState = BUTTON_READY;
			}

			if(state == TIME_HOUR_EDITING && bState == BUTTON_HOLD) {
					state = TIME_MINUTE_EDITING;
					bState = BUTTON_READY;
			}

			if(state == TIME_MINUTE_EDITING && bState == BUTTON_HOLD) {
					state = TIME_DISPLAYING;
					bState = BUTTON_READY;
					time_is_set = true;
			}

			if (state == TIME_HOUR_EDITING && bState == BUTTON_PRESSED) {
					hours = ++hours % 24;
					bState = BUTTON_READY;
			}

			if (state == TIME_MINUTE_EDITING && bState == BUTTON_PRESSED) {
					minutes = ++minutes % 60;
					seconds = 0;
					ticks = 0;
					bState = BUTTON_READY;
			}

			//reset button state in order to avoid button check
			bState = BUTTON_READY;
			break;
		}

		if(state == TIME_DISPLAYING){
			if(0==remaining){
				digit_1 = digit_3 = 12;
				digit_2 = digit_4 = 11;
			}
			else{
				digit_1 = 0;
				digit_2 = remaining / 100;
				digit_3 = (remaining - (digit_2*100)) / 10;
				digit_4 = remaining - ((digit_2*100) + (digit_3*10));
			}
			if( digit_1 == 0 ) digit_1 = GLYPH_none;
			if( digit_2 == 0 ) digit_2 = GLYPH_none;
			if( digit_3 == 0 ) digit_3 = GLYPH_none;
		}
		else if(state == TIME_HOUR_EDITING || state == TIME_MINUTE_EDITING ){
			digit_1 = hour / 10;
			digit_2 = hour % 10;
			digit_3 = minute / 10;
			digit_4 = minute % 10;
			if( digit_1 == 0 ) digit_1 = GLYPH_none;
		}


		//when set time - hour point and the opposite digits won't be shown
		if(state == TIME_HOUR_EDITING) {
			digit_3 = digit_4 = GLYPH_none;
		}
		if(state == TIME_MINUTE_EDITING) {
			digit_1 = digit_2 = GLYPH_none;
		}

		if ( ! go_to_sleep ) {
			display_digit( DIGIT_4, digit_1);
			display_digit( DIGIT_3, digit_2);
			display_digit( DIGIT_2, digit_3);
			display_digit( DIGIT_1, digit_4);
		}
	}
}

#pragma vector = PORT2_VECTOR
__interrupt void PORT2_ISR(void)
{
	bState = BUTTON_PRESSED;
	displaying_watchdog = 0;//reset display watchdog
	if(state == SLEEPING) {
		state = TIME_DISPLAYING;
		bState = BUTTON_READY;
		LPM3_EXIT;
		P2IFG &= ~BUTTON_1; // clear the interrupt flag
		P2IE |= BUTTON_1;
		return;
	}

	P2IFG &= ~BUTTON_1; // clear the interrupt flag
	P2IE |= BUTTON_1;
}

#pragma vector = WDT_VECTOR
__interrupt void WATCHDOG_INTERVAL_TIMER_ISR(void)
{
	// Flag is cleared automatically.
	tick_happened = true;
	++ticks;
	switch ( ticks ) {
	case 64:
		ticks = 0;
		++seconds;

		if ( state == TIME_DISPLAYING || state == PAPA_DISPLAYING ) {
			  ++displaying_watchdog;
			  if ( displaying_watchdog > 14 ) {
				  displaying_watchdog = 0;
				  go_to_sleep = true;
			  }
		}

		if ( seconds == 60 ) {
			seconds = 0;
			++minutes;
			if ( minutes == 60 ) {
				minutes = 0;
				++hours;
				if ( hours == 24 ) {
					hours = 0;
					if(remaining > 0)
						--remaining;
				}
			}

		}
		break;
	}
}
