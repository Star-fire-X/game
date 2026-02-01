"""
Legend2 地图查看器入口文件

用法:
    python main.py [map_file]
"""

import os
import sys
from PyQt5.QtWidgets import QApplication

# 确保本地导入正常工作
CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
if CURRENT_DIR not in sys.path:
    sys.path.append(CURRENT_DIR)

from map_viewer import MapViewerWindow


def main():
    """主函数"""
    app = QApplication(sys.argv)
    app.setApplicationName("Legend2 地图查看器")
    app.setOrganizationName("Legend2")
    
    # 创建主窗口
    window = MapViewerWindow()
    window.show()
    
    # 如果提供了命令行参数，尝试加载地图
    if len(sys.argv) > 1:
        map_path = sys.argv[1]
        if os.path.exists(map_path):
            window._load_map(map_path)
        else:
            print(f"警告: 地图文件不存在: {map_path}")
    
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
