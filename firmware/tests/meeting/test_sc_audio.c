#include <assert.h>
#include <stdlib.h>
#include "../../main/sc_audio.c"
static int wake_result,format_result,sleep_result,sleep_count,sem_available,deleted,notified;
static int force_fallback,fail_realloc,realloc_calls,sem_created,fail_task;
static int read_attempts, delay_calls, read_ok, encode_attempts;
static bool fake_rx_overflow;
bool bsp_audio_rx_overflowed(void){return fake_rx_overflow;}
void bsp_audio_clear_rx_overflow(void){fake_rx_overflow=false;}
const char *esp_err_to_name(esp_err_t e){(void)e;return "error";}
esp_err_t bsp_audio_init(void){return ESP_OK;}
esp_err_t bsp_audio_sleep(void){sleep_count++;return sleep_result;}
esp_err_t bsp_audio_wake(void){return wake_result;}
esp_err_t bsp_audio_set_format(uint32_t hz,uint8_t bits,uint8_t ch){(void)hz;(void)bits;(void)ch;return format_result;}
void bsp_audio_set_volume(uint8_t volume){(void)volume;}
esp_err_t bsp_audio_read(void *data,size_t size){(void)data;(void)size;if(read_ok)return ESP_OK;if(++read_attempts==5)s_run=false;return ESP_FAIL;}
void vTaskDelay(unsigned ticks){assert(ticks>0);delay_calls++;}
void *heap_caps_malloc(size_t n,unsigned caps){(void)caps;if(force_fallback&&n==sizeof(OpusEncoder))return NULL;return malloc(n);}
void *heap_caps_realloc(void *p,size_t n,unsigned caps){(void)caps;realloc_calls++;return fail_realloc?NULL:realloc(p,n);}
size_t heap_caps_get_free_size(unsigned caps){(void)caps;return 100000;}
size_t heap_caps_get_largest_free_block(unsigned caps){(void)caps;return force_fallback?sizeof(OpusEncoder)-1:100000;}
int opus_encoder_get_size(int channels){(void)channels;return sizeof(OpusEncoder);}
int opus_encoder_init(OpusEncoder *e,int rate,int channels,int app){(void)e;(void)rate;(void)channels;(void)app;return OPUS_OK;}
int opus_encoder_ctl(OpusEncoder *e,int request,...){(void)e;(void)request;return 0;}
int opus_encode(OpusEncoder *e,const int16_t *pcm,int samples,uint8_t *data,int cap){(void)e;(void)pcm;(void)samples;(void)data;(void)cap;if(read_ok){if(++encode_attempts==5)s_run=false;return -1;}return 1;}
TaskHandle_t xTaskCreateStatic(void (*fn)(void *),const char *name,unsigned size,void *arg,unsigned prio,StackType_t *stack,StaticTask_t *tcb){(void)fn;(void)name;(void)size;(void)arg;(void)prio;(void)stack;(void)tcb;return fail_task?NULL:(void *)2;}
void vTaskDelete(TaskHandle_t task){assert(task==(void *)2);deleted++;}
void xTaskNotifyGive(TaskHandle_t task){assert(task==(void *)2);notified++;}
unsigned ulTaskNotifyTake(BaseType_t clear,unsigned ticks){(void)clear;(void)ticks;return 0;}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task){(void)task;return 100;}
SemaphoreHandle_t xSemaphoreCreateBinary(void){sem_created++;return (void *)1;}
BaseType_t xSemaphoreTake(SemaphoreHandle_t h,unsigned ticks){(void)h;(void)ticks;if(sem_available){sem_available=0;return pdTRUE;}return pdFALSE;}
BaseType_t xSemaphoreGive(SemaphoreHandle_t h){(void)h;sem_available=1;return pdTRUE;}

static void test_peak_percent(void)
{
    const int16_t peaks[] = {0, 128, 384, -512, 2048, -8192, INT16_MAX, INT16_MIN};
    const uint8_t expected[] = {0, 0, 18, 25, 50, 75, 100, 100};
    for (size_t i = 0; i < sizeof(expected); i++) {
        assert(level_of(&peaks[i], 1) == expected[i]);
    }
    assert(level_of(peaks, 4) == 25);
}

static void test_meter_smoothing(void)
{
    unsigned hold_frames = 0;
    uint8_t level = smooth_level(0, 20, &hold_frames);
    assert(level == 20);
    for (unsigned i = 0; i < 3; i++) {
        level = smooth_level(level, 0, &hold_frames);
        assert(level == 20);
    }
    level = smooth_level(level, 50, &hold_frames);
    assert(level == 50);

    for (unsigned i = 0; i < 6; i++) {
        level = smooth_level(level, 0, &hold_frames);
        assert(level == 50);
    }
    level = smooth_level(level, 0, &hold_frames);
    assert(level == 37);
    for (unsigned i = 0; i < 20; i++) {
        uint8_t previous = level;
        level = smooth_level(level, 0, &hold_frames);
        assert(level < previous || level == 0);
    }
    assert(level == 0);
}

int main(void){
 test_peak_percent();
 test_meter_smoothing();
 assert(sc_audio_init(NULL,NULL)==ESP_OK);assert(sleep_count==1);
 /* Boot releases the idle encoder for TLS; recording must recreate it. */
 assert(s_enc&&sc_audio_release_encoder_idle()&&!s_enc&&deleted==1);
 wake_result=ESP_FAIL;assert(sc_audio_prepare()==ESP_FAIL);assert(!s_codec_prepared&&sleep_count==2);
 wake_result=ESP_OK;format_result=ESP_FAIL;assert(sc_audio_prepare()==ESP_FAIL);assert(!s_codec_prepared&&sleep_count==3);
 sleep_result=ESP_FAIL;assert(sc_audio_prepare()==ESP_FAIL&&s_codec_needs_reset);
 assert(sc_audio_prepare()==ESP_ERR_INVALID_STATE&&s_codec_needs_reset);
 assert(!sc_audio_release_encoder_idle());sleep_result=ESP_OK;
 format_result=ESP_OK;assert(sc_audio_prepare()==ESP_OK&&s_codec_prepared);
 assert(sc_audio_start()==ESP_OK&&notified==1&&!s_quiescent);
 assert(!sc_audio_stop());assert(!sc_audio_release_encoder_idle());
 assert(sc_audio_prepare()==ESP_ERR_INVALID_STATE); /* in-flight capture must not be restarted */
 s_codec_prepared=false;s_quiescent=true;sem_available=1;
 assert(sc_audio_stop()&&sc_audio_release_encoder_idle()&&deleted==2);
 assert(sc_audio_prepare()==ESP_OK&&s_codec_prepared);
 assert(sc_audio_release_encoder_idle()&&deleted==3);
 force_fallback=1;assert(sc_audio_prepare()==ESP_OK&&realloc_calls==1);
 assert(sc_audio_release_encoder_idle()&&deleted==4);
 fail_realloc=1;assert(sc_audio_prepare()==ESP_ERR_NO_MEM&&realloc_calls==2&&!s_enc);
 fail_realloc=0;force_fallback=0;fail_task=1;
 assert(sc_audio_init(NULL,NULL)==ESP_ERR_NO_MEM && sem_created==1);
 fail_task=0;assert(sc_audio_init(NULL,NULL)==ESP_OK);
 assert(sem_created==1); /* Reinitializing after release reuses the stop semaphore. */
 assert(sc_audio_reset_capture_fault()&&!sc_audio_capture_fault());
 fake_rx_overflow=true;assert(sc_audio_capture_fault());
 assert(sc_audio_reset_capture_fault()&&!sc_audio_capture_fault());
 s_run=true;assert(!sc_audio_reset_capture_fault());capture_loop();
 assert(read_attempts==5&&delay_calls==5&&!s_running&&sc_audio_capture_fault());
 /* Pause and stop do not clear the recording-wide fault. */
 assert(sc_audio_stop()&&sc_audio_capture_fault());
 assert(sc_audio_reset_capture_fault()&&!sc_audio_capture_fault());
 read_ok=1;s_run=true;capture_loop();
 assert(encode_attempts==5&&!s_running&&sc_audio_capture_fault());
 assert(sc_audio_reset_capture_fault()&&!sc_audio_capture_fault());
 assert(sc_audio_release_encoder_idle());
 return 0;
}
