from setuptools import setup

package_name = 'pot_monitor_ros2'

setup(
    name=package_name,
    version='0.1.0',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools', 'pyserial'],
    zip_safe=True,
    maintainer='ms.jo',
    maintainer_email='ms.jo@aidinrobotics.co.kr',
    description='Publishes potentiometer values from the STM32 pot_monitor board (USB CDC) as ROS2 topics.',
    license='MIT',
    entry_points={
        'console_scripts': [
            'pot_publisher = pot_monitor_ros2.pot_publisher:main',
        ],
    },
)
