#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>

/* 스마트폰에 표시될 장치 이름 */
#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* 광고(Advertising) 데이터 설정 */
static const struct bt_data ad[] = {
    /* 1. 주변 기기가 검색할 수 있도록 플래그 설정 */
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    /* 2. 장치 이름을 광고 패킷에 포함 */
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

void main(void)
{
    int err;

    /* 1. 블루투스 하드웨어 활성화 */
    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }

    /* 2. 광고 시작 (연결 가능한 상태로 진입) */
    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }

    printk("Advertising started! 스마트폰에서 보드를 찾아주세요.\n");

    /* 메인 루프는 비워두거나 연결 대기를 위해 유지 */
    while (1) {
        k_sleep(K_MSEC(1000));
    }
}