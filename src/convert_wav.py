import wave

# 설정 값 (보드 설정과 동일하게 유지)
RAW_FILENAME = "voice_record.raw"
WAV_FILENAME = "voice_record.wav"
CHANNELS = 1              # 모노
SAMPLE_WIDTH = 2          # 16-bit PCM (2바이트)
FRAMERATE = 16000         # 16kHz

def convert_raw_to_wav():
    try:
        # 1. RAW 파일 읽기
        with open(RAW_FILENAME, 'rb') as raw_file:
            raw_data = raw_file.read()

        # 2. WAV 파일 생성 및 설정
        with wave.open(WAV_FILENAME, 'wb') as wav_file:
            wav_file.setnchannels(CHANNELS)
            wav_file.setsampwidth(SAMPLE_WIDTH)
            wav_file.setframerate(FRAMERATE)
            
            # 데이터 쓰기
            wav_file.writeframes(raw_data)
            
        print(f"변환 완료: {WAV_FILENAME}")
    except FileNotFoundError:
        print(f"오류: {RAW_FILENAME} 파일을 찾을 수 없습니다.")

if __name__ == "__main__":
    convert_raw_to_wav()