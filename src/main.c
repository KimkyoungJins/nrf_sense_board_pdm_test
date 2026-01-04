#include <zephyr/kernel.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/drivers/gpio.h>

/* 회로도 기반 핀 설정 */
#define MIC_PWR_PIN  10
const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

/* 소리 감지 임계값 (상황에 맞게 1000 ~ 10000 사이로 조절하세요) */
#define DETECT_THRESHOLD  1500 

#define MAX_SAMPLE_RATE  16000
#define SAMPLE_BIT_WIDTH 16
#define BYTES_PER_SAMPLE sizeof(int16_t)
#define READ_TIMEOUT     1000 

#define BLOCK_SIZE(_sample_rate, _number_of_channels) \
    (BYTES_PER_SAMPLE * (_sample_rate / 10) * _number_of_channels)

#define MAX_BLOCK_SIZE   BLOCK_SIZE(MAX_SAMPLE_RATE, 1) 
#define BLOCK_COUNT      8 

K_MEM_SLAB_DEFINE_STATIC(mem_slab, MAX_BLOCK_SIZE, BLOCK_COUNT, 4);

static void stream_raw_pdm_data(const struct device *dmic_dev, struct dmic_cfg *cfg)
{
    int ret;

    /* 마이크 전원 켜기 */
    if (!device_is_ready(gpio1_dev)) {
        printk("Error: GPIO1 not ready\n");
        return;
    }
    gpio_pin_configure(gpio1_dev, MIC_PWR_PIN, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set(gpio1_dev, MIC_PWR_PIN, 1);
    k_msleep(500); 

    ret = dmic_configure(dmic_dev, cfg);
    if (ret < 0) return;

    ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
    if (ret < 0) return;

    printk("--- Data Streaming with Detection Mode ---\n");

    while (1) {
        int16_t *buffer;
        uint32_t size;

        ret = dmic_read(dmic_dev, 0, (void **)&buffer, &size, READ_TIMEOUT);
        if (ret == 0) {
            uint32_t num_samples = size / BYTES_PER_SAMPLE;
            int16_t block_max = 0;

            /* 1. 데이터를 계속 출력 (그래프용) */
            for (uint32_t j = 0; j < num_samples; j += 20) {
                printk("%d\n", buffer[j]);

                /* 2. 출력하는 중에 가장 큰 절대값을 체크 */
                int16_t abs_val = (buffer[j] < 0) ? -buffer[j] : buffer[j];
                if (abs_val > block_max) {
                    block_max = abs_val;
                }
            }

            /* 3. 이번 데이터 묶음(Block) 중에 임계값을 넘는 큰 소리가 있었다면 메시지 출력 */
            if (block_max > DETECT_THRESHOLD) {
                // 시리얼 플로터 데이터 사이에서 잘 보이도록 문구 출력
                printk(">> [DETECTED] Peak: %d <<\n", block_max);
            }

            k_mem_slab_free(&mem_slab, buffer);
        } else {
            k_msleep(10);
        }
    }
}

int main(void)
{
    const struct device *const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));
    k_msleep(2000); 

    if (!device_is_ready(dmic_dev)) {
        printk("DMIC device not ready!\n");
        return 0;
    }

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