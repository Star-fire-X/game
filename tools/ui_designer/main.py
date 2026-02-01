import os
import sys
from PyQt5.QtWidgets import QApplication

# Ensure local imports work when launched from repo root
CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
if CURRENT_DIR not in sys.path:
    sys.path.append(CURRENT_DIR)

from ui_designer import UIDesignerWindow  # noqa: E402


def main():
    app = QApplication(sys.argv)
    window = UIDesignerWindow()
    window.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
