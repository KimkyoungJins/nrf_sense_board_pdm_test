/*
 * 통합 프로젝트: DMIC Audio Streaming over BLE NUS
 * 하드웨어: Seeed Studio XIAO nRF52840 Sense
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <bluetooth/services/nus.h> // NUS 서비스 헤더 필수
#include <stdio.h>

// --- [1] 설정 및 매크로 정의 ---
#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

// 마이크 하드웨어 설정 (XIAO Sense 마이크 전원: P1.10)
#define MIC_PWR_PIN     10
static const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

// 오디오 사양
#define MAX_SAMPLE_RATE  16000
#define SAMPLE_BIT_WIDTH 16
#define BYTES_PER_SAMPLE sizeof(int16_t)
#define READ_TIMEOUT     1000

// BLE 전송용 청크 사이즈 (MTU 고려, 안전하게 180~244 바이트 권장)
#define BLE_MAX_CHUNK    244

// 메모리 버퍼 계산
#define BLOCK_SIZE(_sample_rate, _number_of_channels) \
    (BYTES_PER_SAMPLE * (_sample_rate / 10) * _number_of_channels)
#define MAX_BLOCK_SIZE   BLOCK_SIZE(MAX_SAMPLE_RATE, 1)
#define BLOCK_COUNT      8

// 메모리 슬랩 정의 (오디오 데이터 임시 저장소)
K_MEM_SLAB_DEFINE_STATIC(mem_slab, MAX_BLOCK_SIZE, BLOCK_COUNT, 4);

// --- [2] 전역 변수 (BLE 상태 관리) ---
static struct bt_conn *current_conn; // 현재 연결된 스마트폰 정보
static volatile bool nus_connected = false; // 연결 여부 플래그

// --- [3] BLE 관련 콜백 및 설정 ---
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

// 연결 되었을 때 실행
static void connected(struct bt_conn *conn, uint8_t err) {
    if (err) return;
    current_conn = bt_conn_ref(conn);
    nus_connected = true;
    printk("Connected to phone!\n");
}

// 연결 끊겼을 때 실행
static void disconnected(struct bt_conn *conn, uint8_t reason) {
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
        nus_connected = false;
        printk("Disconnected from phone.\n");
    }
}

static struct bt_conn_cb conn_callbacks = {
    .connected    = connected,
    .disconnected = disconnected,
};

// NUS 수신 콜백 (필요 시 구현)
static void bt_receive_cb(struct bt_conn *conn, const uint8_t *const data, uint16_t len) {
    // 폰에서 데이터를 보낼 경우 여기서 처리
}

static struct bt_nus_cb nus_cb = {
    .received = bt_receive_cb,
};

// --- [4] 오디오 수집 및 전송 스레드 (핵심 로직) ---
void audio_thread_fn(void) {
    int ret;
    const struct device *const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));

    printk("[Audio Thread] Waiting for DMIC device...\n");
    if (!device_is_ready(dmic_dev)) {
        printk("[Error] DMIC device not ready!\n");
        return;
    }
    if (!device_is_ready(gpio1_dev)) {
        printk("[Error] GPIO device not ready!\n");
        return;
    }

    // 1. 마이크 전원 켜기
    gpio_pin_configure(gpio1_dev, MIC_PWR_PIN, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set(gpio1_dev, MIC_PWR_PIN, 1);
    k_msleep(500); // 전원 안정화 대기

    // 2. DMIC 설정
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
            .req_num_chan    = 1,
            .req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT),
        },
    };
    cfg.streams[0].pcm_rate = MAX_SAMPLE_RATE;
    cfg.streams[0].block_size = BLOCK_SIZE(cfg.streams[0].pcm_rate, cfg.channel.req_num_chan);

    ret = dmic_configure(dmic_dev, &cfg);
    if (ret < 0) {
        printk("[Error] DMIC config failed: %d\n", ret);
        return;
    }

    ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
    if (ret < 0) {
        printk("[Error] DMIC trigger failed: %d\n", ret);
        return;
    }
    printk("[Audio Thread] Mic started! Ready to stream.\n");

    // 3. 무한 루프: 읽기 -> 보내기
    while (1) {
        // (A) 연결되지 않았으면 대기 (배터리 절약 및 불필요한 처리 방지)
        if (!nus_connected) {
            k_msleep(100);
            continue;
        }

        void *buffer;
        uint32_t size;

        // (B) 마이크 데이터 읽기 (Blocking)
        ret = dmic_read(dmic_dev, 0, &buffer, &size, READ_TIMEOUT);
        
        if (ret == 0 && size > 0) {
            // (C) BLE 전송 (MTU 사이즈에 맞춰 쪼개서 전송)
            uint8_t *data_ptr = (uint8_t *)buffer;
            uint32_t bytes_sent = 0;

            while (bytes_sent < size) {
                // 남은 데이터와 최대 청크 사이즈 중 작은 값을 선택
                uint32_t chunk_len = (size - bytes_sent) > BLE_MAX_CHUNK ? BLE_MAX_CHUNK : (size - bytes_sent);
                
                // NUS를 통해 데이터 전송
                ret = bt_nus_send(current_conn, data_ptr + bytes_sent, chunk_len);

                if (ret < 0) {
                    // 전송 실패(보통 버퍼 꽉 참) 시 잠시 대기 후 재시도 or 건너뛰기
                    // printk("TX Fail: %d\n", ret); 
                    k_msleep(1); 
                    break; // 이번 블록의 남은 데이터는 버림 (실시간성 유지 위해)
                } else {
                    bytes_sent += chunk_len;
                }
            }

            // (D) 사용한 버퍼 메모리 해제 (필수)
            k_mem_slab_free(&mem_slab, buffer);
        } else {
            // 읽기 실패 시
             k_msleep(1);
        }
    }
}

// 스레드 정의 (스택 사이즈 4096, 우선순위 5)
K_THREAD_DEFINE(audio_tid, 4096, audio_thread_fn, NULL, NULL, NULL, 5, 0, 0);


// --- [5] 메인 함수 (시스템 초기화) ---
void main(void)
{
    int err;

    // 1. BLE 초기화
    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }
    printk("Bluetooth initialized.\n");

    // 2. 연결 콜백 등록
    bt_conn_cb_register(&conn_callbacks);

    // 3. NUS 서비스 초기화
    err = bt_nus_init(&nus_cb);
    if (err) {
        printk("Failed to init NUS (err: %d)\n", err);
        return;
    }

    // 4. 광고 시작
    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }
    printk("Advertising started as %s\n", DEVICE_NAME);

    // 메인 스레드는 할 일이 끝났으므로 쉽니다. (오디오 스레드가 따로 돕니다)
    while (1) {
        k_sleep(K_FOREVER);
    }
}