#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include "TMD3725.h"
#include "als_process.h"

static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_received_signal = 0;

static void sigint_sigterm_handler(int signum) {
    g_received_signal = signum;
    g_running = 0;
}

void load_configuration(const char *path, tmd3725_config_t *cfg) {
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("[TMD3725][Config] File %s not found. Using default configuration.\n", path);
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        // Skip empty lines and comments
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r' || line[0] == '\0') {
            continue;
        }

        char key[64];
        char value[64];

        // Parse key=value format
        if (sscanf(line, "%63[^=]=%63s", key, value) == 2) {
            if (strcmp(key, "I2C_BUS") == 0) {
              strncpy(cfg->i2c_bus, value, sizeof(cfg->i2c_bus) - 1);
				cfg->i2c_bus[sizeof(cfg->i2c_bus) - 1] = '\0';
            } else if (strcmp(key, "ATIME") == 0) {
                cfg->atime = (uint8_t)strtoul(value, NULL, 0); // Base 0 recognizes both 0x3F and 63
            } else if (strcmp(key, "CFG0") == 0) {
                cfg->cfg0 = (uint8_t)strtoul(value, NULL, 0);
            } else if (strcmp(key, "PRATE") == 0) {
                cfg->prate = (uint8_t)strtoul(value, NULL, 0);
            } else if (strcmp(key, "PCFG0") == 0) {
                cfg->pcfg0 = (uint8_t)strtoul(value, NULL, 0);
            } else if (strcmp(key, "PCFG1") == 0) {
                cfg->pcfg1 = (uint8_t)strtoul(value, NULL, 0);
            } else if (strcmp(key, "WTIME") == 0) {
                cfg->wtime = (uint8_t)strtoul(value, NULL, 0);
            } else if (strcmp(key, "POLL_TIME_US") == 0) {
                cfg->poll_time_us = (useconds_t)strtoul(value, NULL, 0);
            } else if (strcmp(key, "DETECTION_THRESH") == 0) {
                cfg->prox_det_thresh = (uint8_t)strtoul(value, NULL, 0);
            } else if (strcmp(key, "EMPTY_THRESH") == 0) {
                cfg->prox_emp_thresh = (uint8_t)strtoul(value, NULL, 0);
            } else {
   		 printf("[TMD3725][Config] Unknown configuration key: %s\n", key);
	    }
        }
    }
    fclose(f);
    printf("[TMD3725][Config] Successfully loaded configuration from %s\n", path);
}

int main(void) {

    //Default config
	tmd3725_config_t config = {
        .i2c_bus = "/dev/i2c-1",
        .atime = 0x3F,
        .cfg0 = 0x80,
        .prate = 0x1F,
        .pcfg0 = 0x4F,
        .pcfg1 = 0x4D,
        .wtime = 179,
        .poll_time_us = 300000,
        .prox_det_thresh = 60,
        .prox_emp_thresh = 30
    };
    uint8_t sensor_id = 0;
	load_configuration("/etc/tmd3725.conf", &config);

    struct sigaction sa;
    sa.sa_handler = sigint_sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    int fd = tmd3725_init(config.i2c_bus);
    if (fd < 0) return -1;

    usleep(3000);//Time from power-on to ready to receive I2C commands typical value 1.5ms
    if (tmd3725_verify_id(fd, &sensor_id) != 0) {
        fprintf(stderr, "[TMD3725][Error] Invalid Sensor ID. Expected: 0x%02X, Got: 0x%02X\n",
                TMD3725_DEVICE_ID, sensor_id);
        close(fd);
        return -1;
    }
    printf("[TMD3725] Sensor ID verified successfully, ID=0x%02X\n", sensor_id);

    if (tmd3725_setup(fd, &config) < 0) {
        fprintf(stderr, "[TMD3725][Error] Sensor setup failed\n");
        close(fd);
        return -1;
    }

	als_update_cache();
    // Cycle: ALS(~178ms) + PROX(~12ms) + WTIME(~500ms) = ~690ms, polling the value every 300ms (default values)
    const useconds_t POLL_TIME_US = config.poll_time_us;
	
	// Thresholds for Proximity PDATA detection/release 
	const uint8_t detection_pdata_threshold = config.prox_det_thresh;
    const uint8_t empty_pdata_threshold = config.prox_emp_thresh;
	/*
	Masks to check if the cycle is done
	Proximity is done if PINT or either of PSAT bits is set 
	ALS is done if either AINT or ASAT bit is set
	*/
    const uint8_t PROX_DONE_MASK = TMD3725_STATUS_PINT             |
                                   TMD3725_STATUS_PSAT_REFLECTIVE  |
                                   TMD3725_STATUS_PSAT_AMBIENT;

    const uint8_t ALS_DONE_MASK  = TMD3725_STATUS_AINT |
                                   TMD3725_STATUS_ASAT;

    tmd3725_data_t s_data;
	int detected = 0;
	
    printf("\033[?1049h");   // Enter Alternate screen buffer
    printf("\033[H");
    fflush(stdout);
   
    while (g_running) {

    if (tmd3725_read_all(fd, &s_data) < 0) {
        fprintf(stderr, "[TMD3725][Error] I2C Read all failed\n");
        usleep(POLL_TIME_US);
        continue;
    }

    uint8_t status = s_data.status;

    if ((status & PROX_DONE_MASK) && (status & ALS_DONE_MASK)) {
    
         printf("\033[H");//move to the top
	 printf("\033[J");//clear screen

        //----------------------------------------------------------------
        // PROXIMITY
        //----------------------------------------------------------------
        printf("\n=== Proximity ===\n");
        if (status & (TMD3725_STATUS_PSAT_REFLECTIVE | TMD3725_STATUS_PSAT_AMBIENT)) {
            if (status & TMD3725_STATUS_PSAT_REFLECTIVE) {
                if (!detected) {
                    detected = 1;
                    printf("[PSAT_R] -> OBJECT DETECTED (saturation)\n");
                } else {
                    printf("[PSAT_R] [detected] PDATA=%u\n", s_data.proximity);
                }
            } else {
                printf("[PSAT_A] Ambient saturation - sample skipped\n");
            }
        } else {
            uint8_t pdata = s_data.proximity;
            if (!detected && pdata >= detection_pdata_threshold) {
                detected = 1;
                printf("PDATA: %3u -> OBJECT DETECTED\n", pdata);
            } else if (detected && pdata <= empty_pdata_threshold) {
                detected = 0;
                printf("PDATA: %3u -> EMPTY SPACE\n", pdata);
            } else {
                printf("PDATA: %3u [%s]\n", pdata, detected ? "detected" : "empty");
            }
        }
	    printf("================\n\n");
        // ----------------------------------------------------------------
        // ALS
        // ----------------------------------------------------------------
	
        if (check_saturation(s_data.clear) || (status & TMD3725_STATUS_ASAT)) 
		{
		//Analog saturation (AFE) or saturation ceiling for current ATIME zone
		   printf("[ALS] Saturated (C=%u%s) - skipping measurement\n", s_data.clear,
           (status & TMD3725_STATUS_ASAT) ? ", ASAT" : "");
		} 
		else process_als_cycle(s_data.red, s_data.green, s_data.blue, s_data.clear);
		fflush(stdout);
		
		int again_status = tmd3725_adjust_again(fd, s_data.clear);
		if(again_status < 0)
		fprintf(stderr, "[ALS][Error] Invalid AGAIN adjustment\n");
		if(again_status == 1) als_update_cache();
		// ----------------------------------------------------------------
		// Clear status register
		if (tmd3725_clear_status(fd) < 0)
		fprintf(stderr, "[TMD3725][Error] Invalid Status register clear\n");

    }

    usleep(POLL_TIME_US);
  }
  
    printf("\033[?1049l");  // Exit Alternate screen buffer
    fflush(stdout);

    printf("\n[TMD3725] Received signal %d (%s), attempting to send the sleep mode command\n",
         g_received_signal, strsignal(g_received_signal));
	if (tmd3725_sleep(fd) < 0){
        fprintf(stderr, "[TMD3725][Error] Invalid sleep mode command to Sensor\n");
    } else {
        printf("[TMD3725] Sensor is in sleep mode\n");
    }
	
    close(fd);
    printf("[TMD3725] Application shutdown.\n");
    return 0;
}
