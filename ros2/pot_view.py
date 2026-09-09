"""두 필터값 토픽(/pot/flt0, /pot/flt1)을 한 화면에 같이 보여주는 간단 뷰어.
사용: python3 pot_view.py   (Ctrl+C 종료)
"""
import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from std_msgs.msg import Float32


class PotView(Node):
    def __init__(self):
        super().__init__('pot_view')
        self.v = [None, None]
        self.create_subscription(Float32, 'pot/flt0',
                                 lambda m: self.upd(0, m), qos_profile_sensor_data)
        self.create_subscription(Float32, 'pot/flt1',
                                 lambda m: self.upd(1, m), qos_profile_sensor_data)
        self.create_timer(0.1, self.show)   # 화면 갱신 10 Hz (수신은 100 Hz 그대로)

    def upd(self, i, msg):
        self.v[i] = msg.data

    def show(self):
        a = ' ----' if self.v[0] is None else f'{self.v[0]:5.0f}'
        b = ' ----' if self.v[1] is None else f'{self.v[1]:5.0f}'
        print(f'flt0={a}   flt1={b}', flush=True)


def main():
    rclpy.init()
    node = PotView()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
