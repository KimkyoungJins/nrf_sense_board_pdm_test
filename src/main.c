/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/logging/log.h>
#include <stdlib.h> // abs() 함수 사용을 위해 추가

LOG_MODULE_REGISTER(dmic_sample);

#define MAX_SAMPLE_RATE  16000
#define SAMPLE_BIT_WIDTH 16
#define BYTES_PER_SAMPLE sizeof(int16_t)
#define READ_TIMEOUT     1000

#define BLOCK_SIZE(_sample_rate, _number_of_channels) \
    (BYTES_PER_SAMPLE * (_sample_rate / 10) * _number_of_channels)

#define MAX_BLOCK_SIZE   BLOCK_SIZE(MAX_SAMPLE_RATE, 2)
#define BLOCK_COUNT      4

K_MEM_SLAB_DEFINE_STATIC(mem_slab, MAX_BLOCK_SIZE, BLOCK_COUNT, 4);

static int do_pdm_transfer(const struct device *dmic_dev,
               struct dmic_cfg *cfg,
               size_t block_count)
{
    int ret;

    // 설정 로그는 처음 시작할 때만 출력되도록 유지
    ret = dmic_configure(dmic_dev, cfg);
    if (ret < 0) {
        LOG_ERR("Failed to configure the driver: %d", ret);
        return ret;
    }

    ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
    if (ret < 0) {
        LOG_ERR("START trigger failed: %d", ret);
        return ret;
    }

    // 지정된 횟수만큼 블록을 읽음
    for (int i = 0; i < block_count; ++i) {
        int16_t *buffer; // 데이터를 16비트 정수로 바로 접근하기 위해 형변환 준비
        uint32_t size;

        ret = dmic_read(dmic_dev, 0, (void **)&buffer, &size, READ_TIMEOUT);
        if (ret < 0) {
            LOG_ERR("%d - read failed: %d", i, ret);
            break;
        }

        // --- [실시간 데이터 확인 로직 추가] ---
        uint32_t num_samples = size / BYTES_PER_SAMPLE;
        int32_t sum = 0;
        
        // 간이 음량 계산: 샘플들의 절대값 평균을 구함
        for (uint32_t j = 0; j < num_samples; j++) {
            sum += abs(buffer[j]);
        }
        int32_t avg_volume = sum / num_samples;

        // 터미널에 실시간으로 첫 번째 샘플값과 평균 음량 출력
        // %6d는 정렬을 위해 6자리를 차지하도록 설정한 것임
        printk("Sample[0]: %6d | Avg Volume: %6d | Bytes: %u\n", buffer[0], avg_volume, size);
        // --------------------------------------

        k_mem_slab_free(&mem_slab, buffer);
    }

    // 루프가 끝나면 중지 (무한 루프에서 호출할 경우 실제로는 이 부분에 도달하지 않게 설계 가능)
    dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);

    return ret;
}

int main(void)
{
    const struct device *const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));
    
    // 터미널(picocom)을 켤 시간을 벌기 위해 2초 대기
    k_msleep(2000);
    printk("\n--- PDM Real-time Monitoring Start ---\n");

    if (!device_is_ready(dmic_dev)) {
        LOG_ERR("%s is not ready", dmic_dev->name);
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

    // 모노 설정
    cfg.channel.req_num_chan = 1;
    cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
    cfg.streams[0].pcm_rate = MAX_SAMPLE_RATE;
    cfg.streams[0].block_size = BLOCK_SIZE(cfg.streams[0].pcm_rate, cfg.channel.req_num_chan);

    // [무한 루프 추가] 
    // 프로그램을 종료하지 않고 실시간으로 계속 데이터를 받습니다.
    while (1) {
        // 한 번 호출 시 4개의 블록(0.4초치)을 처리하고 루프를 돎
        int ret = do_pdm_transfer(dmic_dev, &cfg, BLOCK_COUNT);
        if (ret < 0) {
            printk("Transfer error! Restarting...\n");
            k_msleep(1000);
        }
        // CPU가 너무 과열되지 않게 아주 잠깐 쉬어줌
        k_yield();
    }

    return 0;
}