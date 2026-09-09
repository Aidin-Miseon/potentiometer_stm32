=====================================================================
 pot_monitor — STM32F401 가변저항 2채널 USB 모니터
=====================================================================

가변저항 2개(PA0, PA1)를 초당 1000회 읽어 USB 가상 COM으로 출력.
노이즈 필터(중앙값3→EMA)와 최소/최대 캘리브레이션 내장.

■ 필요한 것
  보드: STM32F401RC (크리스탈 25MHz, ST-LINK 없음 → USB DFU로 플래시)
  PC:   STM32CubeIDE 2.1.1, STM32CubeProgrammer  (flash.bat이 자동 사용)

■ 배선
  가변저항 가운데 다리 → PA0 (2번째는 PA1),  양끝 → 3.3V / GND
  ※ 5V 금지.  값이 이상하면 배선 접촉부터 확인

■ 사용법
  1. 코드 수정: Core\Src\main.c  (USER CODE BEGIN~END 사이에만 쓸 것)
  2. 플래시:  BOOT0 High + 리셋 → flash.bat → BOOT0 Low + USB 재연결
  3. 보기:   plot_gui.bat (그래프)  /  read_com.bat (숫자·키 입력)
  ※ COM 포트는 한 번에 하나만 열림. 플래시 전에 창 닫기

■ read_com 출력 형식
  raw0=2226 flt0=2227 min0=---- max0=---- | raw1=... (PA1 동일)
  raw: ADC 원시값 0~4095 (=0~3.3V)   flt: 필터 적용값
  min/max: 캘리브레이션 결과 (----는 아직 안 쟀음, 전원 끄면 사라짐)

■ 키 (read_com 창에서)
  c    둘 다 캘리브레이션: 안내 뜨면 한쪽 끝 7초 → 반대쪽 끝 7초
  0/1  PA0만 / PA1만
  i    보드 정보 (sysclk 확인용)

■ plot_gui
  위=PA0, 아래=PA1.  파랑=원본(raw), 주황=필터(flt).
  상단 체크박스 "필터값만 보기":
    끄면 = 원본 + 필터를 겹쳐 표시 (기본)
    켜면 = 원본을 숨기고 필터값만 표시, 세로축 제목에 필터값의
           σ(표준편차)가 0.5초마다 갱신되어 표시
  최근 10초만 슬라이딩 표시, 세로축 자동 스케일.
  수신은 1000Hz 그대로, 화면 표시만 ~100Hz로 솎아서 그림.

■ ROS2 연동 (ros2\pot_monitor_ros2)

  설치:
    cp -r ros2/pot_monitor_ros2 ~/ros2_ws/src/
    cd ~/ros2_ws && colcon build --packages-select pot_monitor_ros2
    source install/setup.bash

  실행:
    ros2 run pot_monitor_ros2 pot_publisher
    (보드 자동 탐색. 수동 지정: --ros-args -p port:=/dev/ttyACM0)

  구독 토픽:
    /pot/flt0   std_msgs/Float32   PA0 필터값, 0~4095, 약 1000 Hz
    /pot/flt1   std_msgs/Float32   PA1 필터값

  확인:
    ros2 topic echo /pot/flt0
    python3 ros2/pot_view.py          (두 값을 한 화면에)

■ 자주 바꾸는 값 (main.c)
  출력 주기             루프 끝의 1ms 틱 대기 블록 = 초당 1000회.
                       느리게 하려면 그 블록을 HAL_Delay(10) 등으로 교체 (10=초당 100회)
  FILT_EMA_SHIFT (6)   필터 강도. 클수록 부드럽고 느림 (6=÷64, 3=÷8)
                       ※ 출력 주기를 바꾸면 같이 조정 (1000Hz=6, 100Hz=3 권장)
  CALIB_SETTLE_MS / CALIB_MEASURE_MS   캘리브레이션 이동/측정 시간

■ 문제 해결
  장치 못 찾음            BOOT0 Low + 리셋. 장치관리자에 "USB 직렬 장치" 확인
  포트를 열 수 없음        다른 read/plot 창 닫기
  "No STM32 in DFU mode"  BOOT0 High + 리셋 다시
  raw가 0/4095에 고정      배선 접촉 불량 (선 하나씩 흔들며 확인)
  raw가 ~2000에서 부유     그 핀에 아무것도 안 꽂힘

■ 폴더
  Core\Src\main.c     ★ 수정하는 유일한 파일
  pot_monitor.ioc     핀 설정 (CubeMX로 열기 → GENERATE → 빌드)
  tools\              read/plot 스크립트 (지우지 말 것)
  Debug\              빌드 산출물 (자동 생성)
  나머지(.project 등)  CubeIDE/CubeMX 관리 — 수정·이동 금지
  ..\ros2\            ROS2 노드 패키지 + 뷰어 (위 ROS2 연동 참고)

■ 필터 구성
  측정 신호에서 일반 잡음과 순간적인 튐 현상이 함께 발생.
  서로 다른 잡음을 제거하기 위해 2단 필터 적용.

  1단: 중앙값 필터
    - 순간적으로 크게 튀는 임펄스 잡음 제거
    - 신호의 급격한 변화는 최대한 유지

  2단: IIR 저역통과 필터
    - 지속적으로 발생하는 잔여 잡음 감소
    - 실측 잡음: 5.8 → 1.5 count (약 3.9배 감소)

  결과
    - 순간적인 튐 현상과 일반적인 신호 잡음을 동시에 감소
    - 잡음 제거 성능과 응답 속도를 고려하여 필터 계수 설정
=====================================================================