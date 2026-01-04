#include <zephyr/kernel.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/drivers/gpio.h>

#define MIC_PWR_PIN  10
const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

#define MAX_SAMPLE_RATE  16000
#define SAMPLE_BIT_WIDTH 16
#define BYTES_PER_SAMPLE sizeof(int16_t)
#define READ_TIMEOUT     2000 // 타임아웃을 조금 더 늘림

#define BLOCK_SIZE(_sample_rate, _number_of_channels) \
    (BYTES_PER_SAMPLE * (_sample_rate / 10) * _number_of_channels)

#define MAX_BLOCK_SIZE   BLOCK_SIZE(MAX_SAMPLE_RATE, 1) 
#define BLOCK_COUNT      4

K_MEM_SLAB_DEFINE_STATIC(mem_slab, MAX_BLOCK_SIZE, BLOCK_COUNT, 4);

static void stream_raw_pdm_data(const struct device *dmic_dev, struct dmic_cfg *cfg)
{
    int ret;

    // [Step 1] GPIO 확인
    if (!device_is_ready(gpio1_dev)) {
        printk("[DEBUG] Error: GPIO1 device not ready\n");
        return;
    }

    // [Step 2] 마이크 전원 공급 및 대기
    printk("[DEBUG] Powering on Microphone (P1.10)...\n");
    gpio_pin_configure(gpio1_dev, MIC_PWR_PIN, GPIO_OUTPUT_ACTIVE);
    k_msleep(500); // 안정화를 위해 충분히 대기 (500ms)

    // [Step 3] DMIC 설정
    ret = dmic_configure(dmic_dev, cfg);
    if (ret < 0) {
        printk("[DEBUG] Configuration failed: %d\n", ret);
        return;
    }
    printk("[DEBUG] DMIC configured successfully\n");

    // [Step 4] 스트리밍 시작
    ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
    if (ret < 0) {
        printk("[DEBUG] Trigger START failed: %d\n", ret);
        return;
    }
    printk("[DEBUG] Data streaming started. Speak into the mic!\n");

    int block_cnt = 0;
    while (1) {
        int16_t *buffer;
        uint32_t size;

        // [Step 5] 데이터 읽기 시도
        ret = dmic_read(dmic_dev, 0, (void **)&buffer, &size, READ_TIMEOUT);
        if (ret < 0) {
            printk("[DEBUG] Read Error at block %d: %d\n", block_cnt, ret);
            // 만약 -11(EAGAIN)이 뜨면 데이터가 아직 준비 안 된 것임
            k_msleep(10);
            continue; 
        }

        block_cnt++;
        uint32_t num_samples = size / BYTES_PER_SAMPLE;

        // [Step 6] 데이터 모니터링 (10번 블록마다 출력하여 시리얼 부하 감소)
        if (block_cnt % 10 == 0) {
            printk("[DEBUG] Block %d received (%d samples). First sample: %d\n", 
                    block_cnt, num_samples, buffer[0]);
        }

        // 마이크 앞에서 소리를 냈을 때 수치가 변하는지 확인하기 위해
        // 100개 샘플 중 가장 큰 값을 찾아 출력해봅니다 (피크 감지)
        int16_t max_val = 0;
        for (uint32_t i = 0; i < num_samples; i++) {
            if (buffer[i] > max_val) max_val = buffer[i];
        }
        
        // 소리가 감지될 때만 출력 (임계값 500 이상일 때)
        if (max_val > 500 || max_val < -500) {
            printk("Peak detected: %d\n", max_val);
        }

        k_mem_slab_free(&mem_slab, buffer);
    }
}

int main(void)
{
    printk("--- Seeed Studio XIAO nRF52840 PDM Debugger Start ---\n");
    
    const struct device *const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));
    k_msleep(2000); 

    if (!device_is_ready(dmic_dev)) {
        printk("[DEBUG] DMIC device not ready. Check your overlay file!\n");
        return 0;
    }

    struct pcm_stream_cfg stream = {
        .pcm_width = SAMPLE_BIT_WIDTH,
        .mem_slab  = &mem_slab,
    };

    struct dmic_cfg cfg = {
        .io = {
            .min_pdm_clk_freq = 1000000,
            .max_pdm_clk_freq = 3500000,
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