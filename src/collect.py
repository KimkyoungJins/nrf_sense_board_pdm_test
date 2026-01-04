import serial
import time

# [주의] ls /dev/cu.usbmodem* 결과에 맞춰 수정하세요.
PORT = '/dev/cu.usbmodem1101' 
BAUD_RATE = 115200

try:
    # 포트 열기 시도
    ser = serial.Serial(PORT, BAUD_RATE, timeout=0.5)
    time.sleep(1) # 연결 안정화 대기
    ser.reset_input_buffer() # 기존에 쌓여있던 외계어 버퍼 비우기
    
    print(f"포트 연결 성공: {PORT}")
    print("녹음을 시작합니다. 데이터를 수집 중입니다... (Ctrl+C로 종료)")

    with open("voice_record.raw", "wb") as f:
        while True:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                f.write(data)
                # 데이터가 들어오고 있는지 점 모양으로 표시 (.)
                print(".", end="", flush=True) 

except serial.SerialException as e:
    print(f"\n[오류] 포트를 열 수 없습니다. 다른 프로그램이 사용 중인지 확인하세요: {e}")
except KeyboardInterrupt:
    print("\n녹음 완료! voice_record.raw 파일이 생성되었습니다.")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()