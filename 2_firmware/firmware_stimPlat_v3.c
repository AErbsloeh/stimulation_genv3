#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include <math.h>
#include <stdbool.h>


#define CS_PIN 5
#define sck_pin 18
#define mosi_pin 19
#define EN_PWR 0 // Turn on current limit if testing this pin

static const double spi_freq_Hz = 30e6;

static const uint8_t nclr_pin = 3;
static const uint8_t nload_pin = 2;
static const uint8_t start_stim_pin = 6;
static const uint8_t stim_status_pin = 7;
static const uint8_t n_stim_status_pin = 1;

static const uint8_t en_stim_ch0 = 29;
static const uint8_t en_stim_ch1 = 28;
static const uint8_t en_stim_ch2 = 26;
static const uint8_t en_stim_ch3 = 4;


static const bool use_twos_complement = false;
static const bool use_unipolar = false;
static const bool use_set_dac = false;

static const uint16_t length_lockup_global = 200;

static const uint16_t lockup_global [] = {0,259,
517,776,1034,1293,1551,1810,2068,2326,2584,
2842,3099,3356,3614,3871,4127,4384,4640,4896,5151,
5407,5662,5916,6170,6424,6678,6931,7183,7435,7687,
7938,8189,8439,8689,8938,9186,9434,9682,9929,
10175,10420,10665,10909,11153,11396,11638,11879,
12120,12360,12599,12837,13075,13312,13548,13783,
14017,14250,14483,14714,14945,15175,15403,15631,
15858,16084,16309,16533,16755,16977,17198,17417,
17636,17853,18070,18285,18499,18712,18924,19134,
19344,19552,19759,19964,20169,20372,20574,20775,
20974,21172,21369,21564,21758,21951,22142,22332,
22521,22708,22894,23078,23261,23442,23622,23801,
23978,24154,24328,24500,24671,24840,25008,25175,
25339,25503,25664,25824,25983,26139,26295,26448,
26600,26750,26899,27046,27191,27334,27476,27616,
27754,27891,28026,28159,28290,28420,28548,28674,
28798,28921,29041,29160,29277,29393,29506,29618,
29727,29835,29941,30045,30148,30248,30346,30443,
30538,30631,30722,30811,30898,30983,31066,31147,
31227,31304,31379,31453,31524,31594,31662,31727,
31791,31853,31912,31970,32026,32079,32131,32181,
32228,32274,32318,32360,32399,32437,32472,32506,
32538,32567,32595,32620,32644,32665,32684,32702,
32717,32730,32741,32751,32758,32763,32766,32767};

volatile  bool start_stim_irq;
volatile bool timer_fired;
volatile uint16_t dig_value;

void reset_dac(void);

// Write 16 bit to the specified register
void reg_write( spi_inst_t *spi, 
                const uint8_t reg, 
                const uint16_t data) {

    uint8_t msg[3];
                
    // Construct message
    msg[0] = 0x00 | reg;
    msg[1] = (data >> 8);
    msg[2] = data & 0xff;

    // Write to register
    gpio_put(CS_PIN, 0);
    spi_write_blocking(spi, msg, 3);
    gpio_put(CS_PIN, 1);
}

void stim_value( spi_inst_t *spi,
                uint16_t digval,
                uint8_t channel){
    reg_write(spi, channel, digval);
}

int64_t single_timer_callback(alarm_id_t id, __unused void *user_data){
    timer_fired = true;
    stim_value(spi0, dig_value, 4);
    return 0;
};

int64_t single_timer_callback_rectStim(alarm_id_t id, __unused void *user_data){
    timer_fired = true;
    return 0;
};

void reset_dac(void) {

    uint8_t i;
    for (i = 0; i < 3; i++) {
        gpio_put(nclr_pin, 0);
        sleep_ms(0.1);
        gpio_put(nclr_pin, 1);
        sleep_ms(0.1);
    }
}

void start_stim_callback(uint gpio, __unused uint32_t events) {
    start_stim_irq = true;
}

bool repeating_timer_callback(__unused struct repeating_timer *t) {
    timer_fired = true;
    stim_value(spi0, dig_value, 4);
    return true;
}

// returns highest nsteps for the exact target stim frequency and n_channels individual channels
// to be tested.... (seems kind of brute force)
// max_nsteps: limit for nsteps (number of lookup table elements available)
uint16_t calc_nsteps(   float spi_freq_Hz, 
                        float freq_stim_Hz,
                        uint8_t n_channels,
                        uint8_t max_nsteps) {
    
    uint16_t t_wait_min = ceil(1e6 * 30 * 1/spi_freq_Hz); //in us
    uint16_t product = round(1/(n_channels*t_wait_min*1e-6*freq_stim_Hz)); // freq_stim should devider from 1MHz  

    uint16_t t_wait = 0;
    uint16_t i_nsteps = 0;
    for (i_nsteps= 1; i_nsteps<max_nsteps; i_nsteps++) {
        if (product % (4*i_nsteps-3) == 0) {
            return i_nsteps;
        }

    } 
    return 0;
}

uint16_t calc_nsteps_approx(   float spi_freq_Hz, 
                        float freq_stim_Hz,
                        uint8_t n_channels,
                        uint8_t max_nsteps) {
    
    uint16_t t_wait_min = ceil(1e6 * 60 * 1/spi_freq_Hz); //in us
    uint16_t nsteps = floor((1/(n_channels*t_wait_min*1e-6*freq_stim_Hz) + 3) / 4); // freq_stim should devider from 1MHz  

    uint16_t add_wait_us = 1;
    while (nsteps > max_nsteps) {
        nsteps = floor((1/(n_channels*(t_wait_min+add_wait_us)*1e-6*spi_freq_Hz) + 3) / 4);
        add_wait_us++;
    }

    return nsteps;
}

// en_ch: [0,1,2,3,4]
void init_dac(  spi_inst_t *spi,
                uint8_t en_ch,
                bool do_rst,
                bool use_set_dac) {
    
    if (do_rst) {
        reset_dac();
    }

    if (use_set_dac) {
        gpio_put(nload_pin,0);
    }
    
    // Writing to the "Power control register"
    if (en_ch == 4) {
        reg_write(spi, 0b0010000, 15);
    } else {
        reg_write(spi, 0b0010000, 1 << (en_ch));
    }
    
    // Writing to the "Control register"
    reg_write(spi, 0b0011001, 0);

    // Writing to the "Output range select register"
    if (use_unipolar) {
        reg_write(spi, 0b0001000 | en_ch, 0);
    } else {
        reg_write(spi, 0b0001000 | en_ch, 3);
    }
}



void linspace(   float *downsample_indeces,
                uint16_t original_length,
                uint16_t target_length){
    
    float step_width = (float)(original_length-1)/(target_length-1);

    uint16_t i;
    for (i=0; i<(target_length); i+=1) {
        if (i==0) {
            downsample_indeces[i] = 0;
        }
        else if (i==target_length-1) {
            downsample_indeces[i] = original_length-1;
        }
        else {
            downsample_indeces[i] = downsample_indeces[i-1] + step_width;
        }

    }
}

// Stimuate a single sinus puls
//  freq: Hz
//  nsteps: number of steps for quarter of sine
//  channel: [0,1,2,3,4] with 4 activating all channel
//  wait_forstim: boolean if stimulation should be triggered by external signal
//  nStims: number of sinusoidal stimulations 
void stim_sin( spi_inst_t *spi,
                double sine_freq_Hz,
                float sine_amp_uA,
                uint16_t nsteps_quarter_sine,
                uint8_t dac_channel,
                bool cat_first,
                bool wait_forstim_trigger,
                uint32_t nStim_waveform){
    
    // In theory on SPI transfer takes only 24/spi_freq --> 30 = konservative approximation
    double t_spi_transmission_s = 30/spi_freq_Hz;
    t_spi_transmission_s = 2e-6;
    double time_step = 2e-6;

    // Take the bigger value as time_step
    if (t_spi_transmission_s > time_step) {
        time_step = t_spi_transmission_s;
    }

    float sine_amp_ratio = sine_amp_uA/200;

    if (sine_freq_Hz > 10e3) {
        printf("This frequency might not work since its larger than 10kHz!");
    }
    // actual (4*nsteps - 4) DAC values because max, min and 0 are only applied once
    // nsteps need to be devided by nChannel when multiple different stim patterns are used
    if (nsteps_quarter_sine == 0) {
        nsteps_quarter_sine = round(1/(4*time_step*sine_freq_Hz) + 1);
        if (nsteps_quarter_sine > 200){
            nsteps_quarter_sine = 200;
        }
    }
    uint16_t lookup_amp [nsteps_quarter_sine];
    float lookup_ind [nsteps_quarter_sine];

    linspace(lookup_ind, length_lockup_global, nsteps_quarter_sine);

    // generate lookuptable with specified amplitude
    uint16_t i_index;
    uint16_t index_global;
    for (i_index=0; i_index<(nsteps_quarter_sine); i_index+=1) {
        index_global = round(lookup_ind[i_index]);
        lookup_amp[i_index] = round((lockup_global[index_global])*sine_amp_ratio) + 32768;
        //printf("%d\n", index_global);
        //printf("%d\n",lookup_amp[i]);
    }

    uint16_t twait = round(1e6/(sine_freq_Hz*(4*nsteps_quarter_sine-4)));
    //printf("%d\n", twait);
    float freq_stim_real = 1/(sine_freq_Hz*(double)((4*nsteps_quarter_sine-4)*twait)*1e-6);
    twait = 0;
    //printf("Stimulated freqeny: %.2f", freq_stim_real);
    //twait = round(twait - t_spi_transmission_s*1e6);
    //printf("Waiting %d\n", twait);

    //uint16_t dig_value; def volatiel instead
    uint16_t i_stim;
    uint16_t i;

    if (twait>0) {
        timer_fired = false;
        printf("Enabling Timer");
        struct repeating_timer timer;
        add_repeating_timer_us(-twait, repeating_timer_callback, NULL, &timer);
    }
    // outer loop to stimulation multiple sine waveforms
    for (i_stim=0; i_stim<nStim_waveform; i_stim+=1) {
        //printf("%d\n", i_stim);
        // loop through lookuptable 4 times (forward, backwards, inverted forward, inverted backwards) 
        for (i=1; i<(4*nsteps_quarter_sine-3); i+=1) {
            //printf("Index: ");
            //printf("%d\n",i);
            if (i<nsteps_quarter_sine) {
                dig_value = lookup_amp[i];
                //printf("%d\n",i);
            } else if (nsteps_quarter_sine <= i && i <= 2*(nsteps_quarter_sine-1))
            {
                dig_value = lookup_amp[2*(nsteps_quarter_sine-1)-i];
                //printf("%d\n",2*(nsteps-1)-i);
            } else if (2*(nsteps_quarter_sine-1) < i && i <= 3*(nsteps_quarter_sine-1))
            {   
                dig_value = 2*lookup_amp[0]-lookup_amp[i-2*(nsteps_quarter_sine-1)];
                //printf("%d\n", i-2*(nsteps-1));
            } else {
                dig_value = 2*lookup_amp[0]-lookup_amp[4*(nsteps_quarter_sine-1)-i];
                //printf("%d\n", 4*(nsteps-1)-i);
            }

            // USING ANDREAS SCRIPT INSTEAD COULD LOOK LIKE
            //dig_value = fac_max_amp*SINE_WFG() + 32768;
 
            // test this code block! not sure if you can use that large values in formula
            if (cat_first) {
                dig_value = 2*lookup_amp[0] - dig_value;
                //printf("%d\n", dig_value);
            }

            // interrupt stuff to be tested (StimPlat V1.1 needed)
            if (wait_forstim_trigger && i==0 && i_stim == 1) {
                start_stim_irq = false;
                while (!start_stim_irq){
                    tight_loop_contents();
                }
            }

            if (twait > 0) {
                //add_alarm_in_us(twait, single_timer_callback, NULL, true);
            }

            if (twait == 0) {
                stim_value(spi, dig_value, dac_channel);
            }
            
            if (i==1) {
                gpio_put(stim_status_pin,1);
                gpio_put(n_stim_status_pin, 0);
            }

            if (twait > 0) {
                while (!timer_fired) {
                    tight_loop_contents();
                }
                timer_fired = false;
                //sleep_us(twait);
            }
        //printf("%d\n",dig_value);
        }
    }
    gpio_put(stim_status_pin,0);
    gpio_put(n_stim_status_pin, 1);
    
    if (twait> 0) {
            printf("I've been waiting! %d us", twait);
    }
    if (twait <= 0) {
            //printf("Warning: Duration of SPI transmission takes longer than duration of one amplitude step should!");
        }
}

void stim_sin_funct (spi_inst_t *spi,
                double freq,
                float amp,
                uint8_t channel,
                uint16_t nsteps,
                bool cat_first) {
    
    double t_spi = 30/spi_freq_Hz;
    float fac_max_amp = amp/200;

    uint16_t i_nsteps;
    for (i_nsteps=0; i_nsteps<4*(i_nsteps-1); i_nsteps++)
    if (cat_first){
        //dig_value = fac_max_amp*SINE_WFG() + 32768;
    }
    
}

void stim_rect_single( spi_inst_t *spi,
                double freq,
                float amp,
                uint8_t channel,
                bool cat_first,
                bool wait_forstim,
                uint8_t nStims){
        

        uint16_t dig_value_low;
        uint16_t dig_value_high;

        if (!cat_first){
            dig_value_low = round(amp/200 * 32767 + 32768); 
            dig_value_high = round(32767 - amp/200 * 32767);
        } else {
            dig_value_high = round(amp/200 * 32767 + 32768); 
            dig_value_low = round(32767 - amp/200 * 32767);
        }

        double twait = 1/(2*freq);
        twait = round(twait * 1e6);
        //twait = 0;
        //printf("%2.f", twait);

        gpio_put(stim_status_pin,1);
        gpio_put(n_stim_status_pin, 0);
        uint8_t i_stim;
        for (i_stim=0; i_stim<nStims; i_stim++){
        //while(true){
            if (twait > 0) {
                timer_fired = false;
                add_alarm_in_us(twait, single_timer_callback_rectStim, NULL, false);
            }

            stim_value(spi, dig_value_low, channel);
            if (twait > 0) {
                while (!timer_fired) {
                    tight_loop_contents();
                }
                timer_fired = false;
                add_alarm_in_us(twait, single_timer_callback_rectStim, NULL, false);
            }

            stim_value(spi, dig_value_high, channel);
            if (twait > 0) {
                while (!timer_fired) {
                    tight_loop_contents();
                }
                timer_fired = false;
            }
        }
        stim_value(spi, 32768, channel);
        gpio_put(stim_status_pin,0);
        gpio_put(n_stim_status_pin, 1);
}

int main() {

    spi_inst_t *spi = spi0;

    stdio_init_all();

    gpio_init(CS_PIN);
    gpio_set_dir(CS_PIN, GPIO_OUT);
    gpio_put(CS_PIN, 1);

    spi_init(spi, spi_freq_Hz);

    // Set SPI format
    spi_set_format( spi0,   // SPI instance
                    8,      // Number of bits per transfer
                    0,      // Polarity (CPOL)
                    1,      // Phase (CPHA)
                    SPI_MSB_FIRST);

    // Initialize SPI pins
    gpio_set_function(sck_pin, GPIO_FUNC_SPI);
    gpio_set_function(mosi_pin, GPIO_FUNC_SPI);
    
    gpio_init(stim_status_pin);
    gpio_set_dir(stim_status_pin, GPIO_OUT);
    gpio_set_drive_strength(stim_status_pin, GPIO_DRIVE_STRENGTH_2MA);
    gpio_init(n_stim_status_pin);
    gpio_set_dir(n_stim_status_pin, GPIO_OUT);
    gpio_set_drive_strength(n_stim_status_pin, GPIO_DRIVE_STRENGTH_2MA);

    gpio_init(EN_PWR);
    gpio_set_dir(EN_PWR, GPIO_OUT);
    gpio_set_drive_strength(EN_PWR, GPIO_DRIVE_STRENGTH_2MA);
    
    gpio_init(start_stim_pin);
    gpio_set_dir(start_stim_pin, GPIO_IN); 
    gpio_set_irq_enabled_with_callback(start_stim_pin, GPIO_IRQ_EDGE_RISE, true, &start_stim_callback);
    
    gpio_init(nclr_pin);
    gpio_set_dir(nclr_pin, GPIO_OUT);
    gpio_set_drive_strength(nclr_pin, GPIO_DRIVE_STRENGTH_2MA);

    gpio_init(nload_pin);
    gpio_set_dir(nload_pin, GPIO_OUT);
    gpio_set_drive_strength(nload_pin, GPIO_DRIVE_STRENGTH_2MA);

    gpio_init(en_stim_ch0);
    gpio_set_dir(en_stim_ch0, GPIO_OUT);
    gpio_set_drive_strength(en_stim_ch0, GPIO_DRIVE_STRENGTH_2MA);
    gpio_init(en_stim_ch1);
    gpio_set_dir(en_stim_ch1, GPIO_OUT);
    gpio_set_drive_strength(en_stim_ch1, GPIO_DRIVE_STRENGTH_2MA);
    gpio_init(en_stim_ch2);
    gpio_set_dir(en_stim_ch2, GPIO_OUT);
    gpio_set_drive_strength(en_stim_ch2, GPIO_DRIVE_STRENGTH_2MA);
    gpio_init(en_stim_ch3);
    gpio_set_dir(en_stim_ch3, GPIO_OUT);
    gpio_set_drive_strength(en_stim_ch3, GPIO_DRIVE_STRENGTH_2MA);
    
  

    uint8_t en_ch = 4; //4 means all channels
    bool do_rst = true;
    bool do_sync_update = false;
    double freq_stim = 1e3;
    float amp_stim_uA = 100; //uA
    uint16_t n_steps = 100; // per sine quarter
    bool cat_first = true;
    bool wait_forStim = false;
    uint8_t n_Stims = 3;


    init_dac(spi, en_ch, do_rst, do_sync_update);

    // enable voltage supply
    gpio_put(EN_PWR, 0);
    

    sleep_ms(100);
    sleep_ms(7000);


    gpio_put(en_stim_ch0, 1);
    gpio_put(en_stim_ch1, 1);
    gpio_put(en_stim_ch2, 1);
    gpio_put(en_stim_ch3, 1);

    
    while (true) {
        printf("Stimulation...\n");
        stim_sin(spi, freq_stim, amp_stim_uA, n_steps, en_ch, true, wait_forStim, n_Stims);
        sleep_ms(5);
        stim_rect_single(spi, freq_stim, amp_stim_uA, en_ch, false, wait_forStim, n_Stims);
        sleep_ms(200);
    }
}