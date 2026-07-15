#include "TMD3725.h"
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>


/* ================================================================
 * AGAIN Table
 *
 * This table is used for the Auto ALS gain (AGAIN) functionality
 *
 * idx | AGain | CFG1  | CFG2
 *  0  | 0.5x  | 0x00  | 0x00  (AGAINL=0)
 *  1  | 1x    | 0x00  | 0x04
 *  2  | 4x    | 0x01  | 0x04
 *  3  | 16x   | 0x02  | 0x04  <- default
 *  4  | 64x   | 0x03  | 0x04
 *  5  | 128x  | 0x03  | 0x14  (AGAINMAX=1)
 *
 * Table provides all the possible register CFG1(AGAIN) + CFG2(AGAIN/AGAINMAX)
 * combinations for AGAIN, the values are accessed by the current index g_again_idx 
 *
 * ================================================================ */
#define GAIN_LEVELS 6

static const float   GAIN_TABLE[GAIN_LEVELS] = { 0.5f, 1.0f, 4.0f, 16.0f, 64.0f, 128.0f };
static const uint8_t CFG1_TABLE[GAIN_LEVELS] = { 0x00, 0x00, 0x01, 0x02,  0x03,  0x03   };
static const uint8_t CFG2_TABLE[GAIN_LEVELS] = { 0x00, 0x04, 0x04, 0x04,  0x04,  0x14   };

/* ================================================================
 * Default values
 *
 * g_again_idx=3  -> AGAIN=16x CFG1=0x02 CFG2=0x04, same as in the setup
 * g_atime_reg    -> 0x3F 64 cycles ~178ms integration time default value
 *
 * The values WTIME and ATIME once set through config are not changed during the application lifetime.
 * ================================================================ */
static uint8_t g_again_idx  = 3;
static uint8_t g_atime_reg = 0x3F;


/* ================================================================
 * Returns current ALS gain (AGAIN) value
 * ================================================================ */
float als_again(void)
{
    return GAIN_TABLE[g_again_idx];
}

/* ================================================================
 * Returns current ALS integration time in ms
 * Consists of integration steps with one ATIME step = 2.78ms typical (min 2.68, max 2.90)
 * ================================================================ */
float als_atime_ms(void)
{
    return 2.78f * (float)(g_atime_reg + 1);
}

/* ================================================================
 * Returns max ADC count reachable at current ATIME
 * max_value = integration_cycles * 1024 - 1
 * ATIME=0x3F (64 cycles) -> 65535 (full 16-bit range)
 * for ATIME < 63 the ceiling is lowered and the Digital saturation (65535) is
 * physically unreachable regardless of light intensity only the analog saturation
 * can occur here
 * ================================================================ */
static uint16_t als_max_counts(void)
{
    uint32_t m = (uint32_t)(g_atime_reg + 1) * 1024 - 1;
    return (m > 65535u) ? 65535u : (uint16_t)m;
}

/* ================================================================
 * Represents a saturation check
 * ATIME <= 63 (Analog saturation zone): threshold = 75% of max_counts
 * The reason for reducing the threshold for these ATIME values is that
 * here with a shorter integration window a saturating light peak may not be fully captured,
 * making raw counts underestimate the lights true brightness.
 *
 * ATIME >  63 (Digital and Analog saturation zone): threshold = max_counts
 * In this zone the Digital saturation is likely to occur before the analog saturation
 * 
 * Returns 1 if clear channel count has reached/exceeded the threshold, 0 otherwise
 * ================================================================ */
static uint16_t als_saturation_threshold(void)
{
    uint16_t max_c = als_max_counts();
    return (g_atime_reg <= 63) ? (uint16_t)(max_c - max_c / 4) : max_c;
}

int check_saturation(uint16_t rawc)
{
    return (rawc >= als_saturation_threshold());
}

/* ================================================================
 * Opens the I2C device node for the required communication
 * Returns file descriptor, or -1 if open() failed
 * ================================================================ */
int tmd3725_init(const char *i2c_device) {
    int fd = open(i2c_device, O_RDWR);
    if (fd < 0) {
        perror("[TMD3725][Error] Unsuccessful in opening the I2C bus");
        return -1;
    }
    return fd;
}

/* ================================================================
 * Attempts to verify the expected ID of the TMD37253 sensor (0xE4)
 * Returns 0 if successful, -1 if the ID is wrong or an error has occured
 * ================================================================ */
int tmd3725_verify_id(int fd, uint8_t *out_id) {
    uint8_t id = 0;
    if (tmd3725_read_reg(fd, TMD3725_REG_ID, &id, 1) < 0) return -1;
    if (out_id) *out_id = id;
    return (id == TMD3725_DEVICE_ID) ? 0 : -1;
}

/* ================================================================
 * Single-byte register write over I2C
 * From the sensor datasheet a Write transaction consists of:
 * START, CHIP-ADDRESS-WRITE, REGISTER-ADDRESS, DATA BYTE(S), and STOP.
 * Sends [reg_addr, value] in a single ioctl transaction
 * Returns 0 if write successful, -1 otherwise
 * ================================================================ */
int tmd3725_write_reg(int fd, uint8_t reg, uint8_t val)
{
    uint8_t buf[2];
    buf[0] = reg;
    buf[1] = val;

    struct i2c_msg msg;
    struct i2c_rdwr_ioctl_data msgset;

    memset(&msg, 0, sizeof(msg));
	
    msg.addr  = TMD3725_I2C_ADDR;
    msg.flags = 0; //write flag
    msg.len   = 2;
    msg.buf   = buf;

    msgset.msgs  = &msg;
    msgset.nmsgs = 1;

    if (ioctl(fd, I2C_RDWR, &msgset) < 0) 
	{
        fprintf(stderr, "[TMD3725][Error] Write reg 0x%02X=0x%02X failed: %s\n",
                reg, val, strerror(errno));
        return -1;		
	}
    return 0;
}

/* ================================================================
 * Generic multi-byte register read over I2C (consecutive bytes)
 * From the sensor datasheet a Read transaction consists of:
 * START, CHIP-ADDRESS-WRITE, REGISTER-ADDRESS, START, CHIP-ADDRESS-READ, DATA BYTE(S), and STOP
 * This denotes a Repeated-start format (writes reg address and then without a STOP reads len
 * bytes starting at that address into buf).
 * The sensor has an internal 8-bit buffer that stores the register address location.
 * This buffer auto-increments upon each byte transfer and is retained 
 * even after the STOP command is issued and the I2C bus released.
 * This means that future consecutive Read transactions may omit the register address byte when the
 * Read has to be implemented in more than one ioctl transaction.
 * Additionally this also means that the Sensor Read could theoretically work even without the Repeated-start
 * format (with a write-STOP-read format)
 * Returns 0 if read successful, -1 otherwise
 * ================================================================ */
int tmd3725_read_reg(int fd, uint8_t reg, uint8_t *buf, uint8_t len)
{
    if (!buf || len == 0) return -1;

    uint8_t reg_addr = reg;

    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data msgset;

    memset(msgs, 0, sizeof(msgs));
	//write starting register address
    msgs[0].addr  = TMD3725_I2C_ADDR;
    msgs[0].flags = 0;  //write flag
    msgs[0].len   = 1;
    msgs[0].buf   = &reg_addr;
	//read len number of bytes from the starting address
    msgs[1].addr  = TMD3725_I2C_ADDR;
    msgs[1].flags = I2C_M_RD; //read flag
    msgs[1].len   = len;
    msgs[1].buf   = buf;

    msgset.msgs  = msgs;
    msgset.nmsgs = 2;

    if (ioctl(fd, I2C_RDWR, &msgset) < 0)
	{
		fprintf(stderr, "[TMD3725][Error] Read reg 0x%02X (len=%u) failed: %s\n",
            reg, len, strerror(errno));
        return -1;
	}
    return 0;
}

 /* ================================================================
 * Burst-reads STATUS through PDATA (0x93-0x9C) registers (10 bytes total)
 * Unpacks the 10-byte buffer into a tmd3725_data_t structure (status/clear/red/green/blue/proximity)
 * Returns 0 if successful, -1 otherwise
 * ================================================================ */
 int tmd3725_read_all(int fd, tmd3725_data_t *data)
{
    if (!data) return -1;

    uint8_t buf[10];
    if (tmd3725_read_reg(fd, TMD3725_REG_STATUS, buf, 10) < 0) return -1;

    data->status    = buf[0];
    data->clear     = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
    data->red       = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);
    data->green     = (uint16_t)buf[5] | ((uint16_t)buf[6] << 8);
    data->blue      = (uint16_t)buf[7] | ((uint16_t)buf[8] << 8);
    data->proximity = buf[9];

    return 0;
}

/* ================================================================
 * Runs proximity offset/crosstalk calibration
 * Does optical+electrical calibration 
 * Sensor must have no target in front of it during this process
 * Returns 0 if calibration successful, -1 otherwise
 * ================================================================ */
int tmd3725_calibrate_offset(int fd) {
    uint8_t status   = 0;
    int elapsed_ms   = 0;
    const int timeout_ms = 250;

    // CALIBCFG: 
	// BINSRCH_TARGET=4 - Proximity Result after calibration Target PDATA=15
    // AUTO_OFFSET_ADJ=1 - The value in POFFSETL(Offset Magnitude) register decrements when PDATA equals zero
	// PROX_AVG=4 - Each proximity integration cycle averages 4 seperate proximity measurements
    if (tmd3725_write_reg(fd, TMD3725_REG_CALIBCFG, 0x8A) < 0) return -1;

    // CALIB: ELECTRICAL_CALIBRATION=0 (optical+electrical), START_OFFSET_CALIB=1
    if (tmd3725_write_reg(fd, TMD3725_REG_CALIB, 0x01) < 0) return -1;

    // Polling CALIBSTAT bit0 (CALIB_FINISHED) every 20ms
    // Timeout after 250ms
    do {
        usleep(20000);
        elapsed_ms += 20;
        if (tmd3725_read_reg(fd, TMD3725_REG_CALIBSTAT, &status, 1) < 0) return -1;
        if (status & 0x01) break;
        if (elapsed_ms >= timeout_ms) {
            fprintf(stderr, "[Proximity][Error] Proximity calibration timeout after %dms\n", timeout_ms);
            return -1;
        }
    } while (1);

    // Clearing CALIB_FINISHED and START_OFFSET_CALIB
    if (tmd3725_write_reg(fd, TMD3725_REG_CALIBSTAT, 0x01) < 0) return -1;
    if (tmd3725_write_reg(fd, TMD3725_REG_CALIB, 0x00) < 0) return -1;
    
    return 0;
}

 /* ================================================================
 * Configures ALS, Proximity and Shared registers, then
 * runs Proximity offset calibration and enables both functionalities
 *
 * ALS:   ATIME=0x3F -> 64 cycles, ~178ms, max_counts=65535 (default value)
 *        CFG1=0x02, CFG2=0x04 -> AGAIN=16x, IR_TO_GREEN=0 (Green photodiode is connected to ADC), 
 *        AZ_CONFIG=0x7F -> autozero once before the first ALS cycle
 *
 * Proximity(default values) :  
 *		  PRATE=0x1F -> ~2.816ms/sample, total 4 × 2.816ms = 11.3ms (PROX_AVG=4),
 *		  PCFG0=0x4F -> 8us pulses, 16 pulses max
 *        PCFG1=0x4D -> PGAIN=2x, ~84mA LED drive
 *
 * Shared: WTIME=179 -> ~500ms wait between cycles(default value),
 *         PERS=0x00 -> AINT/PINT is set every cycle, no persistence filtering,
 *         CFG3=0x0C -> INT_READ_CLEAR=0, status flags cleared manually, SAI=0
 *
 * Returns 0 if setup successful, -1 otherwise
 * ================================================================ */
int tmd3725_setup(int fd, const tmd3725_config_t *cfg) {
    // put sensor to sleep
    if (tmd3725_write_reg(fd, TMD3725_REG_ENABLE, 0x00) < 0) return -1;
	g_atime_reg = cfg->atime;
	
    // ALS
	if (tmd3725_write_reg(fd, TMD3725_REG_ATIME, g_atime_reg) < 0) return -1;
    if (tmd3725_write_reg(fd, TMD3725_REG_CFG1,      0x02) < 0) return -1;
    if (tmd3725_write_reg(fd, TMD3725_REG_CFG2,      0x04) < 0) return -1;
    if (tmd3725_write_reg(fd, TMD3725_REG_AZ_CONFIG, 0x7F) < 0) return -1;

    // Proximity
	if (tmd3725_write_reg(fd, TMD3725_REG_PRATE, cfg->prate) < 0) return -1;
    if (tmd3725_write_reg(fd, TMD3725_REG_PCFG0, cfg->pcfg0) < 0) return -1;
    if (tmd3725_write_reg(fd, TMD3725_REG_PCFG1, cfg->pcfg1) < 0) return -1;
	
    // Shared
	if (tmd3725_write_reg(fd, TMD3725_REG_WTIME, cfg->wtime) < 0) return -1;
	if (tmd3725_write_reg(fd, TMD3725_REG_CFG0,  cfg->cfg0) < 0) return -1;
    if (tmd3725_write_reg(fd, TMD3725_REG_PERS,  0x00) < 0) return -1;
    if (tmd3725_write_reg(fd, TMD3725_REG_CFG3,  0x0C) < 0) return -1;

    // PON=1 needed for the proximity calibration
    if (tmd3725_write_reg(fd, TMD3725_REG_ENABLE, TMD3725_ENABLE_PON) < 0) return -1;

    printf("[TMD3725] Starting Proximity calibration (The space in front of the sensor should be clear)...\n");
    if (tmd3725_calibrate_offset(fd) < 0) return -1;
    printf("[TMD3725] Proximity calibration finished.\n");

    // Set AEN=1 and PON=1 in the same command to ensure auto-zero function
    // is run prior to the first measurement
    uint8_t en = TMD3725_ENABLE_PON | TMD3725_ENABLE_AEN |
                 TMD3725_ENABLE_PEN | TMD3725_ENABLE_WEN;
    if (tmd3725_write_reg(fd, TMD3725_REG_ENABLE, en) < 0) return -1;

    return 0;
}

/* ================================================================
 * Adjusts AGAIN depending on the value of the clear channel ADC count
 * Thresholds:
 * UPPER = 75% max_counts — reduce AGAIN before Saturation 
 * LOWER = 15% max_counts — increase AGAIN for better precision
 *
 * Writes to CFG1(AGAIN) and CFG2(AGAINL/AGAINMAX) from the AGAIN table
 * combinations (if the AGAIN needs to be changed).
 * Starts a new autozero calibration if AGAIN was changed
 *
 * Returns:  1 = AGAIN changed
 *           0 = No changes
 *          -1 = I2C Error
 * ================================================================ */
int tmd3725_adjust_again(int fd, uint16_t clear_counts)
{
    
    uint16_t max_c = als_max_counts();
	uint16_t upper = (uint16_t)(max_c - max_c / 4);                        
	uint16_t lower = (uint16_t)((uint32_t)max_c * 15 / 100);

    uint8_t new_idx = g_again_idx;

    if (clear_counts >= upper && g_again_idx > 0)
        new_idx = g_again_idx - 1;
    else if (clear_counts <= lower && g_again_idx < GAIN_LEVELS - 1)
        new_idx = g_again_idx + 1;

    if (new_idx == g_again_idx) return 0;
    
    if (tmd3725_write_reg(fd, TMD3725_REG_CFG1, CFG1_TABLE[new_idx]) < 0) return -1;
    if (tmd3725_write_reg(fd, TMD3725_REG_CFG2, CFG2_TABLE[new_idx]) < 0) return -1;
      
	//Force a new autozero calibration before the next ALS cycle(resets az-done flag)
    if (tmd3725_write_reg(fd, TMD3725_REG_AZ_CONFIG, 0x7F) < 0) return -1;

    printf("[ALS] AGAIN: %.1fx → %.1fx  (C=%u, upper=%u, lower=%u)\n",
       GAIN_TABLE[g_again_idx], GAIN_TABLE[new_idx],
       clear_counts, upper, lower);

    g_again_idx = new_idx;
    return 1;
}

/* ================================================================
 * Clears all STATUS register interrupt/saturation flags
 * ================================================================ */
int tmd3725_clear_status(int fd)
{
    const uint8_t flags = TMD3725_STATUS_PINT             |
                           TMD3725_STATUS_CINT            |
                           TMD3725_STATUS_PSAT            |
                           TMD3725_STATUS_PSAT_REFLECTIVE |
                           TMD3725_STATUS_PSAT_AMBIENT    |
                           TMD3725_STATUS_AINT            |
                           TMD3725_STATUS_ASAT;
    return tmd3725_write_reg(fd, TMD3725_REG_STATUS, flags);
}

/* ================================================================
 * Disables PON/AEN/PEN/WEN, putting the sensor into Sleep state.
 * ================================================================ */
int tmd3725_sleep(int fd)
{
    return tmd3725_write_reg(fd, TMD3725_REG_ENABLE, 0x00);
}