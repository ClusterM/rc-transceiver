#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include "rc-transceiver.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexey 'Cluster' Avdyukhin");
MODULE_DESCRIPTION("rc_transceiver driver for Omega2");
MODULE_VERSION("1.0");

static int pwm_channel = -1;
static int rx_pin = -1;

module_param(rx_pin, int, S_IRUGO);
MODULE_PARM_DESC(rx_pin,"RC receiver pin number");
module_param(pwm_channel, int, S_IRUGO);
MODULE_PARM_DESC(pwm_channel,"RC transmitter PWM channel number");

static int majorNumber = -1;
static struct class*  rc_transceiver_class  = NULL;
static struct device* rc_device = NULL;
static int bus_number_opens = 0;
static struct file* opened_files[MAX_OPENED_FILES];

static void* pwm_regs = NULL;
static struct hrtimer* tx_timer = NULL;
static rctime_t tx_buffer[BUFFER_SIZE];
static u16 tx_buffer_size = 0;
static u16 tx_buffer_pos = 0;
static DEFINE_MUTEX(tx_mutex);
static u8 tx_active = 0;

static struct gpio_desc* rx_pin_desc = NULL;
static u32 rx_irq_number = 0xffff;
static struct hrtimer* rx_timer = NULL;
static u64 rx_start_time = 0;
static rctime_t rx_buffer[BUFFER_SIZE];
static u16 rx_buffer_pos = 0;
static DECLARE_WAIT_QUEUE_HEAD(rx_wq);

// The prototype functions for the character driver -- must come before the struct definition
static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
// static loff_t  dev_llseek(struct file *file,loff_t offset, int orig);
 
static struct file_operations fops =
{
   .open = dev_open,
   .read = dev_read,
   .write = dev_write,
   .release = dev_release,
   .llseek = 0 //dev_llseek
};
 
/* IRQ fired every rising/falling edge of receiver pin */
static irq_handler_t rx_irq_handler(unsigned int irq,
    void *dev_id, struct pt_regs *regs)
{
    u64 now = ktime_to_us(ktime_get_boottime());
    rctime_t time_since_first;

    // ignore signals while transmitting
    if (tx_active)
        return (irq_handler_t) IRQ_HANDLED;

    // limit to buffer size
    if (rx_buffer_pos >= BUFFER_SIZE)
        return (irq_handler_t) IRQ_HANDLED;

    if (
        (((rx_buffer_pos % 2) == 0) && gpiod_get_value(rx_pin_desc))     // must be low
        || (((rx_buffer_pos % 2) == 1) && !gpiod_get_value(rx_pin_desc)) // must be high
        ) 
        return (irq_handler_t) IRQ_HANDLED;

    if (rx_buffer_pos == 0) {
        rx_start_time = now;
    }
    // store time since first impulse
    time_since_first = now - rx_start_time;
    // filter
    if ((rx_buffer_pos > 0) && (time_since_first - rx_buffer[rx_buffer_pos - 1] < RX_FILTER_MIN_PULSE_US)) {
        // noise
        if (rx_buffer_pos >= 2) {
            rx_buffer[rx_buffer_pos - 2] = time_since_first;
            rx_buffer_pos -= 1;
        } else {
            rx_buffer_pos = 0;
        }
    } else {
        // stable signal
        rx_buffer[rx_buffer_pos] = time_since_first;
        rx_buffer_pos++;
        // schedule timeout timer
        if (rx_timer) hrtimer_try_to_cancel(rx_timer);
        hrtimer_start(rx_timer, ktime_set(0, RX_TIMEOUT_USEC * 1000UL), HRTIMER_MODE_REL);
    }

    return (irq_handler_t) IRQ_HANDLED;
}

/* RX timeout timer callback */
static enum hrtimer_restart rx_timeout_callback(struct hrtimer *timer)
{
    int i;
    rcfile_t *rcf;
    // transmition finished
    if (rx_buffer_pos > RX_FILTER_MIN_COUNT) {
        // convert relative timings to absolute
        for (i = 1; i < rx_buffer_pos; i++) {
            rx_buffer[i - 1] = rx_buffer[i] - rx_buffer[i - 1];
        }
        rx_buffer_pos--;
        // send data to clients
        for (i = 0; i < bus_number_opens; i++) {
            rcf = (rcfile_t*)opened_files[i]->private_data;
            if (!rcf->rx_pending) {
                memcpy(rcf->rx_buffer, rx_buffer, rx_buffer_pos * sizeof(rctime_t));
                rcf->rx_size = rx_buffer_pos;
                rcf->rx_pos_nibbles = 0;
                rcf->rx_pending = 1;
                wake_up_interruptible(&rx_wq);
            }
        }
    }
    rx_buffer_pos = 0;
    rx_start_time = 0;
    return HRTIMER_NORESTART;
}

/* Function to schedule TX timer */
static void set_tx_timer(void)
{
    uint32_t enable;
    uint32_t reg_offset = 0x40 * pwm_channel;
    uint16_t duration = 40000000 / TX_CARRIER_FREQ;
    uint16_t duration_h = duration / 2;
    uint16_t duration_l = duration / 2;

    if (tx_timer) hrtimer_try_to_cancel(tx_timer);

    enable = REG_READ(PWM_ENABLE);
    enable &= ~((uint32_t)(1 << pwm_channel));
    REG_WRITE(PWM_ENABLE, enable);

    if (tx_buffer_pos >= tx_buffer_size) {
        tx_active = 0;
        mutex_unlock(&tx_mutex);
        return; // done
    }

    if ((tx_buffer_pos % 2) == 0) {
        REG_WRITE(PWM0_CON + reg_offset, 0x7000 | CLKSEL_40MHZ | CLKDIV_1);
        REG_WRITE(PWM0_HDURATION + reg_offset, duration_h - 1);
        REG_WRITE(PWM0_LDURATION + reg_offset, duration_l - 1);
        REG_WRITE(PWM0_GDURATION + reg_offset, (duration_h + duration_l) / 2 - 1);
        REG_WRITE(PWM0_SEND_DATA0 + reg_offset, 0x55555555);
        REG_WRITE(PWM0_SEND_DATA1 + reg_offset, 0x55555555);
        REG_WRITE(PWM0_WAVE_NUM + reg_offset, 0);

        enable |= 1 << pwm_channel;
        REG_WRITE(PWM_ENABLE, enable);
    }

    hrtimer_start(tx_timer, ktime_set(0, tx_buffer[tx_buffer_pos] * 1000UL), HRTIMER_MODE_REL);
    tx_buffer_pos++;
}

/* TX timer callback */
static enum hrtimer_restart tx_callback(struct hrtimer *timer)
{
    set_tx_timer();
    return HRTIMER_NORESTART;
}

/* Function to start transmit */
static void transmit(rctime_t *seq, int len)
{
    memcpy(tx_buffer, seq, len * sizeof(rctime_t));
    tx_buffer_size = len;
    tx_buffer_pos = 0;
    if (len) set_tx_timer();
}

/* Function to free all resources */
static void rc_transceiver_free(void)
{
    if (!IS_ERR_OR_NULL(rc_transceiver_class)) {
        // remove the device
        device_destroy(rc_transceiver_class, MKDEV(majorNumber, DEV_MINOR));
        // unregister the device class
        class_unregister(rc_transceiver_class);
        // remove the device class
        class_destroy(rc_transceiver_class);
    }
    // unregister the major number
    if (majorNumber >= 0) {
        unregister_chrdev(majorNumber, DEVICE_BUS);
    }

    if (tx_timer) {
        hrtimer_try_to_cancel(tx_timer);
        kfree(tx_timer);
    }

    if (rx_timer) {
        hrtimer_try_to_cancel(rx_timer);
        kfree(rx_timer);
    }

    // unmap registers
    if (pwm_regs != NULL)
        iounmap(pwm_regs);

    // free IRQ
    if (rx_irq_number != 0xffff)
        free_irq(rx_irq_number, NULL);
    // free RX pin        
    if (!IS_ERR_OR_NULL(rx_pin_desc))
        gpiod_put(rx_pin_desc);
}

/* Function to init the module */
static __init int rc_transceiver_init(void)
{
    int r;

    if ((rx_pin < 0) && (pwm_channel < 0)) {
        printk(KERN_ERR "rc-transceiver: You must specify either rx_pin or pwm_channel\n");
        return -1;
    }

    // register character device and request major number
    majorNumber = register_chrdev(0, DEVICE_BUS, &fops);
    if (majorNumber < 0) {
        printk(KERN_ERR "rc-transceiver: failed to register a major number\n");
        rc_transceiver_free();
        return -1;
    }
    // register the device class
    rc_transceiver_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(rc_transceiver_class)) {
        printk(KERN_ERR "rc-transceiver: failed to register device class: %ld\n", PTR_ERR(rc_transceiver_class));
        rc_transceiver_free();
        return -1;
    }
    // register the device driver
    rc_device = device_create(rc_transceiver_class, NULL, MKDEV(majorNumber, DEV_MINOR), NULL, DEVICE_NAME);
    if (IS_ERR(rc_device)) {
        printk(KERN_ERR "rc-transceiver: failed to create the TX device: %ld\n", PTR_ERR(rc_device));
        rc_transceiver_free();
        return -1;
    }

    if (rx_pin >= 0) {
        // prepare pin for the receiver
        rx_pin_desc = gpio_to_desc(rx_pin);
        if (IS_ERR(rx_pin_desc)) {
            printk(KERN_ERR "rc-transceiver: rx_pin gpiod_request error: %ld\n", PTR_ERR(rx_pin_desc));
            rc_transceiver_free();
            return -1;
        }
        // input
        gpiod_direction_input(rx_pin_desc);
        // prepare IRQ for the receiver
        rx_irq_number = gpiod_to_irq(rx_pin_desc);
        r = request_irq(
            // The interrupt number requested
            rx_irq_number,
            // The pointer to the handler function below */
            (irq_handler_t) rx_irq_handler,
            /* Interrupt on falling edge */
            IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
            /* Used in /proc/interrupts to identify the owner */
            "rc_handler",
            NULL);
        if (r) {
            printk(KERN_ERR "transceiver: rx_pin request_irq error\n");
            rc_transceiver_free();
            return -1;
        }
        // allocate and init timer for receiver
        rx_timer = kzalloc(sizeof(struct hrtimer), GFP_KERNEL);
        if (!rx_timer) {
            printk(KERN_ERR "rc-transceiver: can't allocate memory for timer\n");
            rc_transceiver_free();
            return -1;
        }
        hrtimer_init(rx_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        rx_timer->function = rx_timeout_callback;
    }

    if (pwm_channel >= 0) {
        // PWM registers for transmitter
        pwm_regs = ioremap_nocache(PWM_BASE, PWM_SIZE);
        if (pwm_regs == NULL) {
            printk(KERN_ERR "rc-transceiver: failed to map physical memory\n");
            rc_transceiver_free();
            return -1;
        }
        // allocate and init timer for transmitter
        tx_timer = kzalloc(sizeof(struct hrtimer), GFP_KERNEL);
        if (!tx_timer) {
            printk(KERN_ERR "rc-transceiver: can't allocate memory for timer\n");
            rc_transceiver_free();
            return -1;
        }
        hrtimer_init(tx_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        tx_timer->function = tx_callback;
    }

    printk(KERN_INFO "rc-transceiver: driver started\n");
    return 0;
}

/* Function to unload module */
static void __exit rc_transceiver_exit(void)
{
    rc_transceiver_free();    
    printk(KERN_INFO "rc-transceiver: driver stopped\n");
}

static int dev_open(struct inode *inodep, struct file *filep)
{
    rcfile_t *rcf;
    if (bus_number_opens >= MAX_OPENED_FILES)
        return -EMFILE;
    filep->private_data = kzalloc(sizeof(rcfile_t), GFP_KERNEL);
    if (!filep->private_data)
        return -ENOMEM;
    rcf = (rcfile_t*)filep->private_data;
    rcf->id = bus_number_opens;
    rcf->tx_pos_nibbles = 0;
    rcf->rx_size = 0;
    rcf->rx_pos_nibbles = 0;
    rcf->rx_pending = 0;
    opened_files[bus_number_opens] = filep;
    bus_number_opens++;
    return 0;
}
 
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    int lpos;
    char c;
    u8 b;
    ssize_t r = 0;
    rcfile_t *rcf = (rcfile_t*)filep->private_data;

    // no data yet
    if (!rcf->rx_pending) {
        if (filep->f_flags & O_NONBLOCK)
            return -EAGAIN;
        if (wait_event_interruptible(rx_wq, rcf->rx_pending))
            return -ERESTARTSYS;
    }

    for (; rcf->rx_pending && (r < len); r++, rcf->rx_pos_nibbles++, buffer++) {
        lpos = rcf->rx_pos_nibbles / 4;

        if (lpos >= rcf->rx_size) {
            c = '\n';
            // reset
            rcf->rx_size = 0;
            rcf->rx_pos_nibbles = 0;
            rcf->rx_pending = 0;
        } else {
            switch (rcf->rx_pos_nibbles % 4) {
                case 0:
                    b = (rcf->rx_buffer[lpos] >> 4) & 0xF;
                    break;
                case 1:
                    b = rcf->rx_buffer[lpos] & 0xF;
                    break;
                case 2:
                    b = (rcf->rx_buffer[lpos] >> 12) & 0xF;
                    break;
                case 3:
                    b = (rcf->rx_buffer[lpos] >> 8) & 0xF;
                    break;
            }
            if (b < 10) {
                c = '0' + b;
            } else {
                c = 'a' - 10 + b;
            }
        }
        put_user(c, buffer);
    }

    return r;
}


static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    ssize_t r = 0;
    int lpos; 
    char c;
    u8 b;
    rcfile_t *rcf = (rcfile_t*)filep->private_data;

    for (r = 0; r < len; r++, buffer++) {
        lpos = rcf->tx_pos_nibbles / 4;

        if (get_user(c, buffer)) {
            return -EFAULT;
        }

        if (c == '\r' || c == '\n') {
            if (lpos > 0) {
                if ((lpos % 2) == 1) {
                    rcf->tx_buffer[lpos] = TX_FINAL_GAP_USEC;
                    rcf->tx_pos_nibbles += 4;
                } else {
                    rcf->tx_buffer[lpos - 1] = TX_FINAL_GAP_USEC;
                }
                if (mutex_lock_interruptible(&tx_mutex)) {
                    return -ERESTARTSYS;
                }
                tx_active = 1;
                transmit(rcf->tx_buffer, lpos);
            }
            rcf->tx_pos_nibbles = 0;
            continue;
        }

        // limit to buffer size
        if (lpos >= BUFFER_SIZE) {
            if (r) return r;
            return -EFAULT;
        }

        b = 0;
        if (c >= '0' && c <= '9')
            b = c - '0';
        else if (c >= 'a' && c <= 'f')
            b = c + 10 - 'a';
        else if (c >= 'A' && c <= 'F')
            b = c + 10 - 'A';

        switch (rcf->tx_pos_nibbles % 4) {
            case 0:
                rcf->tx_buffer[lpos] = b << 4;
                break;
            case 1:
                rcf->tx_buffer[lpos] = (rcf->tx_buffer[lpos] & 0xFFF0) | b;
                break;
            case 2:
                rcf->tx_buffer[lpos] = (rcf->tx_buffer[lpos] & 0x0FFF) | (b << 12);
                break;
            case 3:
                rcf->tx_buffer[lpos] = (rcf->tx_buffer[lpos] & 0xF0FF) | (b << 8);
                break;
        }
        rcf->tx_pos_nibbles++;
    }
    return r;
}

static int dev_release(struct inode *inodep, struct file *filep)
{
    int id;
    bus_number_opens--;
    id = ((rcfile_t*)filep->private_data)->id;
    opened_files[id] = opened_files[bus_number_opens];
    ((rcfile_t*)opened_files[id]->private_data)->id = id;
    kfree(filep->private_data);
    return 0;
}

module_init(rc_transceiver_init);
module_exit(rc_transceiver_exit);
