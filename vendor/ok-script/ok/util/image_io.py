"""Use Python file handles so OpenCV codecs also work with Unicode paths."""
from pathlib import Path

import cv2
import numpy as np


def imread(path, flags=cv2.IMREAD_COLOR):
    try:
        data = Path(path).read_bytes()
    except OSError:
        return None
    if not data:
        return None
    return cv2.imdecode(np.frombuffer(data, dtype=np.uint8), flags)


def imwrite(path, image, params=None):
    path = Path(path)
    ok, encoded = cv2.imencode(path.suffix, image, [] if params is None else params)
    if not ok:
        return False
    try:
        path.write_bytes(encoded.tobytes())
    except OSError:
        return False
    return True
