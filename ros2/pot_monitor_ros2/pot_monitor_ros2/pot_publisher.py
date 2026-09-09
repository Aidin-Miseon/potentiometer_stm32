"""pot_monitor 보드(USB CDC)의 필터값을 ROS2 토픽으로 발행하는 브리지 노드.

보드 출력(100 Hz):
  raw0=2226 flt0=2227 min0=---- max0=---- | raw1=3496 flt1=3495 min1=---- max1=----

발행 토픽 (필터값만):
  /pot/flt0   std_msgs/Float32   1번 가변저항(PA0) 필터값
  /pot/flt1   std_msgs/Float32   2번 가변저항(PA1) 필터값

파라미터:
  port   시리얼 포트. 기본 '' = 자동 탐색 (USB VID 0483 / PID 5740)
         예: -p port:=/dev/ttyACM0

실행:
  ros2 run pot_monitor_ros2 pot_publisher
  ros2 topic echo /pot/flt0
"""

import re

import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from std_msgs.msg import Float32

import serial
from serial.tools import list_ports

USB_VID = 0x0483  # STMicroelectronics
USB_PID = 0x5740  # STM32 Virtual ComPort (CDC)

RE_NEW = re.compile(r'flt0=\s*(\d+).*?flt1=\s*(\d+)')
RE_OLD = re.compile(r'raw0=\s*(\d+).*?raw1=\s*(\d+)')  # 구버전 펌웨어(flt 없음) 호환


class PotPublisher(Node):
    def __init__(self):
        super().__init__('pot_publisher')
        self.declare_parameter('port', '')

        self.pub0 = self.create_publisher(Float32, 'pot/flt0', qos_profile_sensor_data)
        self.pub1 = self.create_publisher(Float32, 'pot/flt1', qos_profile_sensor_data)

        self.ser = None
        self.rxbuf = b''
        # 보드가 10ms마다 한 줄을 보내므로 5ms 폴링이면 밀리지 않는다
        self.timer = self.create_timer(0.005, self.poll)
        self.get_logger().info('pot_publisher 시작 (포트 탐색 중...)')

    # ---------- 시리얼 ----------
    def find_port(self) -> str:
        configured = self.get_parameter('port').value
        if configured:
            return configured
        for info in list_ports.comports():
            if info.vid == USB_VID and info.pid == USB_PID:
                return info.device
        return ''

    def ensure_open(self) -> bool:
        if self.ser is not None and self.ser.is_open:
            return True
        port = self.find_port()
        if not port:
            self.get_logger().warn('보드를 찾지 못함 (USB 연결 확인)',
                                   throttle_duration_sec=5.0)
            return False
        try:
            self.ser = serial.Serial(port, 115200, timeout=0)
            self.ser.dtr = True
            self.ser.reset_input_buffer()
            self.rxbuf = b''
            self.get_logger().info(f'{port} 연결됨')
            return True
        except (serial.SerialException, OSError) as e:
            self.get_logger().warn(f'{port} 열기 실패: {e}',
                                   throttle_duration_sec=5.0)
            self.ser = None
            return False

    # ---------- 주기 처리 ----------
    def poll(self):
        if not self.ensure_open():
            return
        try:
            data = self.ser.read(4096)
        except (serial.SerialException, OSError):
            self.get_logger().warn('시리얼 끊김 - 재연결 시도')
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None
            return
        if not data:
            return

        self.rxbuf += data
        *lines, self.rxbuf = self.rxbuf.split(b'\n')  # 마지막 조각은 미완성 줄
        for raw_line in lines:
            self.handle_line(raw_line.decode('ascii', errors='ignore').strip())

    def handle_line(self, line: str):
        m = RE_NEW.search(line)
        if not m:
            m = RE_OLD.search(line)
            if not m:
                return  # CALIB/info 등 상태 메시지는 무시
        flt0, flt1 = float(m.group(1)), float(m.group(2))

        self.pub0.publish(Float32(data=flt0))
        self.pub1.publish(Float32(data=flt1))


def main(args=None):
    rclpy.init(args=args)
    node = PotPublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if node.ser is not None and node.ser.is_open:
            node.ser.close()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
