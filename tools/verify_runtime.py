"""Verify our backend without operating other applications' UI."""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from PySide6.QtWidgets import QApplication
from floatshot import native
from floatshot.diagnostics import verify_backend

native.enable_dpi()
app = QApplication([])
root = Path(".test-artifacts/runtime")
report = verify_backend(app, root)
(root / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps(report, ensure_ascii=True, indent=2))
