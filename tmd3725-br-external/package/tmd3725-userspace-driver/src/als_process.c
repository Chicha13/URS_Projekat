#include "als_process.h"
#include "TMD3725.h"
#include <stdio.h>


//TMD3725 Open Air Coefficients
#define DGF         	  682.85f
#define C_COEF        		0.16f
#define R_COEF       	   -0.04f
#define G_COEF        		0.16f
#define B_COEF       	   -0.29f
#define CT_COEF    		  4520.0f
#define CT_OFFSET   	  1804.0f
#define COLOR_SAT_THRESH    0.75f


/* ================================================================
 * Cached values for ATIME, AGAIN, and CPL
 * ================================================================ */
static float g_again_value = 1.0f;
static float g_atime_value = 1.0f;
static float g_cpl_value   = 1.0f; 

/* ================================================================
 * Called once after initialization and then only when AGAIN value is adjusted
 * Updates ATIME, AGAIN, and CPL values
 * ================================================================ */
void als_update_cache(void)
{
    g_again_value = als_again();
    g_atime_value = als_atime_ms();
    g_cpl_value = (g_again_value * g_atime_value) / DGF;
}

/* ================================================================
 * LUX
 * Computes photopic illuminance from raw RGBC counts using
 * default open-air coefficients (DGF, R/G/B/C_COEF).
 * CPL value depends on current ALS gain/integration time
 * Returns 0 for darkness, low signal, or negative results
 * ================================================================ */
float compute_lux(uint16_t rawr, uint16_t rawg,
                  uint16_t rawb, uint16_t rawc)
{
    if (rawc < 10) return 0.0f;		//Dark ADC count value ~2 (noise)
	
    float lux = (C_COEF*(float)rawc + R_COEF*(float)rawr +
                G_COEF*(float)rawg + B_COEF*(float)rawb) / g_cpl_value;
    return (lux < 0.0f) ? 0.0f : lux;
}

/* ================================================================
 * CCT
 * Estimates correlated color temperature of the light from the blue/red ratio.
 * No IR compensation - the sensors hardware UV/IR filters make this unnecessary.
 * Returns -1 when the red/blue channel count number is too low 
 * to give an adequate ratio.
 * ================================================================ */
float compute_CCT(uint16_t rawb, uint16_t rawr)
{
    if (rawr < 10 || rawb < 10) return -1.0f;
    return CT_COEF * ((float)rawb / (float)rawr) + CT_OFFSET;
}


/* ================================================================
 * Hue and Color Saturation
 * Computes hue and color saturation from raw RGB counts
 * COLOR_SAT_THRESH marks the point of the beginning of color saturation past which
 * CCT is no longer meaningful (light too far from a blackbody/planckian source), and 
 * Lux is less reliable.
 * Hue denotes the general color of a color saturated light, for neutral light
 * it is less stable.
 * Returns 0  when all channels are near the noise floor.
 * ================================================================ */

int compute_hue_saturation(uint16_t rawr, uint16_t rawg, uint16_t rawb,
                 float *hue, float *saturation)
{
    *hue = 0.0f;
    *saturation = 0.0f;

    //find max and min channel count
    uint16_t M_raw = rawr;
    uint16_t m_raw = rawr;

    if (rawg > M_raw) M_raw = rawg;
    if (rawb > M_raw) M_raw = rawb;

    if (rawg < m_raw) m_raw = rawg;
    if (rawb < m_raw) m_raw = rawb;

	if (M_raw < 10) return 0;
    //Chroma(range) of the channels
    uint16_t chroma_raw = M_raw - m_raw;
    float C = (float)chroma_raw;

    //Chroma 0 pure neutral light
    if (chroma_raw == 0) {
        *saturation = 0.0f;
        *hue = 0.0f;
        return 1;
    }

    //Color saturation value
    *saturation = C / (float)M_raw;
	
	//Hue is only reliable for color saturated light
    float H_tmp;
    if (M_raw == rawr) {
        //The red channel is dominant
        H_tmp = ((float)rawg - (float)rawb) / C;
    } else if (M_raw == rawg) {
        //The green channel is dominant
        H_tmp = 2.0f + ((float)rawb - (float)rawr) / C;
    } else {
        //The blue channel is dominant
        H_tmp = 4.0f + ((float)rawr - (float)rawg) / C;
    }

    // Get the degree values for hue
    *hue = H_tmp * 60.0f;
    if (*hue < 0.0f) {
        *hue += 360.0f;
    }
	/*
		0°   red
		60°  yellow
		120° green
		180° cyan
		240° blue
		300° magenta
		360° red
	*/
	return 1;
}

/* ================================================================
 * CCT -> CIE (x,y) Kang, Moon, Hong, Lee, Cho and Kim (2002) method
 * Valid for CCT values 1667K–25000K otherwise returns 0
 * Should be noted that this will always give a (x,y) value on the Planckian locus
 * ================================================================ */
int CCT_to_xy(float CCT, float *cx, float *cy)
{
    *cx = 0.0f; *cy = 0.0f;
    if (CCT < 1667.0f || CCT > 25000.0f) return 0;
    float T=CCT, T2=T*T, T3=T2*T, xc, yc;
    if (T <= 4000.0f)
        xc = -0.2661239e9f/T3 - 0.2343589e6f/T2 + 0.8776956e3f/T + 0.179910f;
    else
        xc = -3.0258469e9f/T3 + 2.1070379e6f/T2 + 0.2226347e3f/T + 0.240390f;
    float xc2=xc*xc, xc3=xc2*xc;
    if      (T <= 2222.0f) yc = -1.1063814f*xc3 - 1.34811020f*xc2 + 2.18555832f*xc - 0.20219683f;
    else if (T <= 4000.0f) yc = -0.9549476f*xc3 - 1.37418593f*xc2 + 2.09137015f*xc - 0.16748867f;
    else                   yc =  3.0817580f*xc3 - 5.87338670f*xc2 + 3.75112997f*xc - 0.37001483f;
    *cx = xc; *cy = yc;
	return 1;
}


/* ================================================================
 * Process ALS cycle
 * Represents the entire pipeline for processing raw RGBC sensor data
 * Invokes Hue and Color Saturation, Lux, CCT, CIE (x,y) functions
 * It checks for the validity of returned data from these functions
 * and applies the color saturation treshold.
 * Prints out results of the processing of data from the ALS integration cycle.
 * ================================================================ */

void process_als_cycle(uint16_t rawr, uint16_t rawg,
             uint16_t rawb, uint16_t rawc)
{
    float hue = 0.0f, saturation = 0.0f;
    int hs_valid = compute_hue_saturation(rawr, rawg, rawb, &hue, &saturation);

    float lux = compute_lux(rawr, rawg, rawb, rawc);

    float CCT_value = compute_CCT(rawb, rawr);

	int   cct_numeric_valid = (CCT_value > 0.0f);
	float cx = 0.0f, cy = 0.0f;
	int   cct_xy_valid = cct_numeric_valid && CCT_to_xy(CCT_value, &cx, &cy);
					  
	int colored = hs_valid && (saturation >= COLOR_SAT_THRESH);

    printf("\n=== ALS ===\n");
    printf("Raw: C=%5u R=%5u G=%5u B=%5u  [AGAIN=%.1fx  ATIME=%.1fms]\n",
           rawc, rawr, rawg, rawb, g_again_value, g_atime_value);

	if (colored) {
			printf("Lux=%.2f [Color Saturation Lux unreliable]\n", lux);
		} else {
			printf("Lux=%.2f\n", lux);
		}

    if (!hs_valid) {
        printf("Hue/Saturation: Noise Threshold (RGB channels < 10 counts) - not measured\n");
		printf("CCT/(x,y): not measured\n");
    } else {
        printf("H=%.1f  S=%.3f  [%s]\n",
               hue, saturation, colored ? "Colored Light" : "Neutral Light(Hue less stable)");

        if (colored)
            printf("Colored (S>=%.2f) -> CCT/(x,y) not meaningful for this light source\n",
                   COLOR_SAT_THRESH);
    }
	
	if (hs_valid && saturation < COLOR_SAT_THRESH) {
		if (!cct_numeric_valid) {
			printf("CCT: red/blue channel count too low - not measured\n");
		} 
		else if (!cct_xy_valid) {
        printf("CCT=%.0fK (out of Planckian range 1667-25000K) - (x,y) not computed\n",
               CCT_value);
		} 
		else {
        printf("CCT=%.0fK  xy=(%.4f,%.4f)\n", CCT_value, cx, cy);
		}
	}


    printf("===========\n\n");
}

