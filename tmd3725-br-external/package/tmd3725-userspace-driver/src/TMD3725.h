#ifndef TMD3725_H
#define TMD3725_H
#include <stdint.h>
#include <unistd.h>

#define TMD3725_I2C_ADDR    0x39
#define TMD3725_DEVICE_ID   0xE4

// Proximity related registers
#define TMD3725_REG_PRATE        0x82
#define TMD3725_REG_PILT         0x88
#define TMD3725_REG_PIHT         0x8A
#define TMD3725_REG_PDATA        0x9C
#define TMD3725_REG_PCFG0        0x8E
#define TMD3725_REG_PCFG1        0x8F

// ALS related registers
#define TMD3725_REG_ATIME        0x81
#define TMD3725_REG_CFG1         0x90
#define TMD3725_REG_CFG2         0x9F
#define TMD3725_REG_AZ_CONFIG    0xD6

// Shared registers
#define TMD3725_REG_ENABLE       0x80
#define TMD3725_REG_WTIME        0x83
#define TMD3725_REG_ID           0x92
#define TMD3725_REG_STATUS       0x93
#define TMD3725_REG_CDATAL       0x94
#define TMD3725_REG_CFG3         0xAB
#define TMD3725_REG_PERS         0x8C
#define TMD3725_REG_CFG0         0x8D

// Proximity calibration
#define TMD3725_REG_POFFSETL     0xC0
#define TMD3725_REG_POFFSETH     0xC1
#define TMD3725_REG_CALIB        0xD7
#define TMD3725_REG_CALIBCFG     0xD9
#define TMD3725_REG_CALIBSTAT    0xDC

// Enable mask bits
#define TMD3725_ENABLE_PON       0x01
#define TMD3725_ENABLE_AEN       0x02
#define TMD3725_ENABLE_PEN       0x04
#define TMD3725_ENABLE_WEN       0x08

// Status register clear (0x93) mask bits
#define TMD3725_STATUS_ASAT             0x80
#define TMD3725_STATUS_PSAT             0x40
#define TMD3725_STATUS_PINT             0x20 
#define TMD3725_STATUS_AINT             0x10
#define TMD3725_STATUS_CINT             0x08 
#define TMD3725_STATUS_PSAT_REFLECTIVE  0x02 
#define TMD3725_STATUS_PSAT_AMBIENT     0x01 


//Config structure
typedef struct {
    char     i2c_bus[64];
    uint8_t  atime;
    uint8_t  cfg0;
    uint8_t  prate;
    uint8_t  pcfg0;
    uint8_t  pcfg1;
    uint8_t  wtime;
    useconds_t poll_time_us;
    uint8_t  prox_det_thresh;
    uint8_t  prox_emp_thresh;
} tmd3725_config_t;

//Data structure
typedef struct {
    uint16_t clear;
    uint16_t red;
    uint16_t green;
    uint16_t blue;
    uint8_t  proximity;
} tmd3725_data_t;

int tmd3725_init(const char *i2c_device);
int tmd3725_verify_id(int fd);
int tmd3725_write_reg(int fd, uint8_t reg, uint8_t val);
int tmd3725_read_reg(int fd, uint8_t reg, uint8_t *buf, uint8_t len);
//int tmd3725_read_all(int fd, tmd3725_data_t *data);
int tmd3725_read_status(int fd, uint8_t *status);
int tmd3725_read_data(int fd, tmd3725_data_t *data);
int tmd3725_setup(int fd, const tmd3725_config_t *cfg);
int tmd3725_calibrate_offset(int fd);
int tmd3725_adjust_again   (int fd, uint16_t clear_counts);
int tmd3725_clear_status(int fd);
int tmd3725_sleep(int fd);

float als_again     (void);
float als_atime_ms  (void);
int   check_saturation      (uint16_t rawc);


#endif
