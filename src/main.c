#include <zephyr/kernel.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/drivers/gpio.h>
#include <stdio.h>

/* [하드웨어 설정] */
#define MIC_PWR_PIN  10  // 회로도 상 P1.10 (마이크 전원)
const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

/* [오디오 기술 사양] */
#define MAX_SAMPLE_RATE  16000 
#define SAMPLE_BIT_WIDTH 16    
#define BYTES_PER_SAMPLE sizeof(int16_t)
#define READ_TIMEOUT     1000  

/* [메모리 버퍼 설정] */
#define BLOCK_SIZE(_sample_rate, _number_of_channels) \
    (BYTES_PER_SAMPLE * (_sample_rate / 10) * _number_of_channels)

#define MAX_BLOCK_SIZE   BLOCK_SIZE(MAX_SAMPLE_RATE, 1) 
#define BLOCK_COUNT      8 

K_MEM_SLAB_DEFINE_STATIC(mem_slab, MAX_BLOCK_SIZE, BLOCK_COUNT, 4);

/* [바이너리 전송 함수] */
// printk는 문자열로 변환하므로 느립니다. putchar를 사용하여 8비트씩 직접 전송합니다.
static void send_binary_16bit(int16_t sample)
{
    // Little-endian 방식 전송 (하위 바이트 먼저, 상위 바이트 나중)
    putchar(sample & 0xFF);         
    putchar((sample >> 8) & 0xFF);  
}

static void stream_raw_pdm_data(const struct device *dmic_dev, struct dmic_cfg *cfg)
{
    int ret;

    /* 1. 마이크 전원 ON */
    if (!device_is_ready(gpio1_dev)) return;
    gpio_pin_configure(gpio1_dev, MIC_PWR_PIN, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set(gpio1_dev, MIC_PWR_PIN, 1);
    k_msleep(500); 

    /* 2. DMIC 설정 및 시작 */
    ret = dmic_configure(dmic_dev, cfg);
    if (ret < 0) return;

    ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
    if (ret < 0) return;

    // 파이썬에서 데이터 시작 지점을 알 수 있도록 루프 진입 전에는 아무것도 출력하지 마세요.

    while (1) {
        int16_t *buffer;
        uint32_t size;

        /* 3. 데이터 읽기 */
        ret = dmic_read(dmic_dev, 0, (void **)&buffer, &size, READ_TIMEOUT);
        if (ret == 0) {
            uint32_t num_samples = size / BYTES_PER_SAMPLE;

            /* 4. 바이너리 전송 실행 */
            // 모든 샘플을 전송해야 끊김 없는 소리가 녹음됩니다.
            for (uint32_t j = 0; j < num_samples; j++) {
                send_binary_16bit(buffer[j]);
            }

            k_mem_slab_free(&mem_slab, buffer);
        } else {
            k_msleep(1); // 오류 시 짧은 대기
        }
    }
}

int main(void)
{
    const struct device *const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));
    
    // USB 시리얼이 안정화될 때까지 대기
    k_msleep(2000); 

    if (!device_is_ready(dmic_dev)) return 0;

    struct pcm_stream_cfg stream = {
        .pcm_width = SAMPLE_BIT_WIDTH,
        .mem_slab  = &mem_slab,
    };

    struct dmic_cfg cfg = {
        .io = {
            .min_pdm_clk_freq = 1200000, 
            .max_pdm_clk_freq = 2800000,
            .min_pdm_clk_dc   = 40,
            .max_pdm_clk_dc   = 60,
        },
        .streams = &stream,
        .channel = {
            .req_num_streams = 1,
        },
    };

    cfg.channel.req_num_chan = 1;
    cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
    cfg.streams[0].pcm_rate = MAX_SAMPLE_RATE;
    cfg.streams[0].block_size = BLOCK_SIZE(cfg.streams[0].pcm_rate, cfg.channel.req_num_chan);

    stream_raw_pdm_data(dmic_dev, &cfg);

    return 0;
}