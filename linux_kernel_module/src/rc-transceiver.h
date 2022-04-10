#ifndef _RC_TRANSCIEIVER_H_
#define _RC_TRANSCIEIVER_H_

#define CLASS_NAME  "rc"
#define DEVICE_BUS "rc"
#define DEVICE_NAME "rc"
#define DEV_MINOR 0

// Interval between transmissions
#define TX_FINAL_GAP_USEC 50000
// Carrier frequency for transmitter
#define TX_CARRIER_FREQ 36000
// Recevier gap time between frames
#define RX_TIMEOUT_USEC 10000
// Receiver noise filter: minimum frame length (3 = 1 signal + 1 gap +1 signal)
#define RX_FILTER_MIN_COUNT 3
// Receiver noise filter: minimum signal/gap length
#define RX_FILTER_MIN_PULSE_US 50
// Mamimum count of signal and gaps interval
#define BUFFER_SIZE 256
// How many pseudo-files can be opened
#define MAX_OPENED_FILES 32

#define PWM_BASE 0x10005000
#define PWM_SIZE 32 + 0x40 * 4
#define PWM_ENABLE 0x00000000        // PWM Enable register
#define PWM0_CON          0x00000010 // PWM0 Control register
#define PWM0_HDURATION    0x00000014 // PWM0 High Duration register
#define PWM0_LDURATION    0x00000018 // PWM0 Low Duration register
#define PWM0_GDURATION    0x0000001C // PWM0 Guard Duration register
#define PWM0_SEND_DATA0   0x00000030 // PWM0 Send Data0 register
#define PWM0_SEND_DATA1   0x00000034 // PWM0 Send Data1 register
#define PWM0_WAVE_NUM     0x00000038 // PWM0 Wave Number register
#define PWM0_DATA_WIDTH   0x0000003C // PWM0 Data Width register
#define PWM0_THRESH       0x00000040 // PWM0 Thresh register
#define PWM0_SEND_WAVENUM 0x00000044 // PWM0 Send Wave Number register

#define CLKSEL_100KHZ 0b00000000 
#define CLKSEL_40MHZ  0b00001000

#define CLKDIV_1      0b00000000
#define CLKDIV_2      0b00000001
#define CLKDIV_4      0b00000010
#define CLKDIV_8      0b00000011
#define CLKDIV_16     0b00000100
#define CLKDIV_32     0b00000101
#define CLKDIV_64     0b00000110
#define CLKDIV_128    0b00000111

#define REG_WRITE(addr, value) iowrite32(value, pwm_regs + addr)
#define REG_READ(addr) ioread32(pwm_regs + addr)

typedef u32 rctime_t;

struct rcfile {
    u16 id;
    rctime_t tx_buffer[BUFFER_SIZE];
    u16 tx_pos_nibbles;
    rctime_t rx_buffer[BUFFER_SIZE];
    u16 rx_size;
    u16 rx_pos_nibbles;
    u8 rx_pending;
};

typedef struct rcfile rcfile_t;

#endif
