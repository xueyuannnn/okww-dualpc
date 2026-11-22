"""Guide navigation with a verified top page and overlapping row observations."""
from dataclasses import dataclass
import re

import cv2
import numpy as np


@dataclass
class BookRow:
    button: object
    identity: np.ndarray
    title: str = ''
    y: float = 0.0


def similarity(left, right):
    if len(left.title) >= 3 and left.title == right.title:
        return 1.0
    if left.title and right.title and left.title != right.title:
        return 0.0
    if left.identity.std() < 3:
        return 0.0
    search = cv2.copyMakeBorder(right.identity, 2, 2, 3, 3, cv2.BORDER_REPLICATE)
    score = cv2.matchTemplate(search, left.identity, cv2.TM_CCOEFF_NORMED).max()
    return float(score) if np.isfinite(score) else 0.0


def same_page(previous, current):
    return bool(previous) and len(previous) == len(current) and all(
        similarity(a, b) >= .86 and abs(a.y - b.y) <= .008 for a, b in zip(previous, current))


def overlap(previous, current):
    best, best_score = None, 0.0
    for offset in range(len(previous)):
        count = min(len(previous) - offset, len(current))
        if count < 2:
            continue
        scores = [similarity(previous[offset + i], current[i])
                  if previous[offset + i].y - current[i].y >= -.008 else 0 for i in range(count)]
        matched = sum(value >= .82 for value in scores)
        quality = matched + sum(value for value in scores if value >= .82) / count
        if matched >= 2 and all(value >= .82 for value in scores[1:]) and quality > best_score:
            best, best_score = offset, quality
    return best


def read_rows(task):
    frame = task.frame
    height, width = frame.shape[:2]
    area = task.box_of_screen(.90, .205, .97, .88)
    buttons = list(task.find_feature('boss_proceed', box=area, threshold=.76))
    if getattr(task, '_native_vision', {}).get('desktop', False):
        buttons.extend(task.find_feature('boss_proceed', box=area, threshold=.72, use_gray_scale=True))
    text = task.ocr(box=task.box_of_screen(.39, .18, .97, .88), threshold=.65)
    title = lambda line: str(getattr(line, 'text', getattr(line, 'name', ''))).strip()
    for line in text:
        if line.x >= width * .90 and re.fullmatch(r'前往|传送|傳送|Proceed|Go\s*to|Teleport', title(line), re.I):
            buttons.append(line)
    unique = []
    for button in sorted(buttons, key=lambda b: getattr(b, 'confidence', 0), reverse=True):
        if not any(abs((other.y + other.height / 2) - (button.y + button.height / 2)) < height * .018
                   for other in unique):
            unique.append(button)
    rows = []
    for button in sorted(unique, key=lambda b: b.y + b.height / 2):
        y = (button.y + button.height / 2) / height
        top, bottom = round(max(.18, y - .10) * height), round(min(.88, y + .025) * height)
        crop = frame[top:bottom, round(.40 * width):round(.87 * width)]
        identity = cv2.resize(cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY), (192, 48), interpolation=cv2.INTER_AREA)
        identity = cv2.GaussianBlur(identity, (3, 3), .55)
        names = [re.sub(r'\W', '', title(line)).lower() for line in text
                 if width * .40 <= line.x + line.width / 2 <= width * .87
                 and top <= line.y + line.height / 2 < button.y + button.height / 2]
        rows.append(BookRow(button, identity, '|'.join(names), y))
    return rows


def _locate_book_target(task, serial, total, from_end=False):
    if type(serial) is not int or type(total) is not int or not 1 <= serial <= total <= 1000:
        raise ValueError('索拉指南目标序号超出范围')

    def scroll(amount):
        task.scroll_relative(.68, .55, amount)
        task.sleep(.45)

    current = []
    for reset in range(3):
        scroll(-100 if from_end else 100)
        previous = read_rows(task)
        task.sleep(.2)
        current = read_rows(task)
        # Two independent upward resets also work when compression hides the thumb.
        if reset >= 1 and same_page(previous, current):
            break
    else:
        raise RuntimeError('索拉指南未确认列表顶部，未点击副本')
    first, stalled = 1, 0
    for _ in range(total * 6 + 24):
        requested = len(current) - serial + 1 if from_end else serial
        if from_end and len(current) < serial:
            break
        if first <= requested < first + len(current):
            previous = current
            task.sleep(.2)
            current = read_rows(task)
            if same_page(previous, current):
                row = current[requested - first]
                return row
            stalled += 1
            if stalled >= 4:
                break
            continue
        previous = current
        scroll(-2)
        current = read_rows(task)
        offset = overlap(previous, current)
        if offset is None:
            scroll(1)
            current = read_rows(task)
            offset = overlap(previous, current)
            if offset is None:
                break
            stalled += 1
        elif offset == 0:
            stalled += 1
        else:
            stalled = 0
        first += offset
        if stalled >= 5:
            break
    task.screenshot('索拉指南选行未确认')
    raise RuntimeError(f'索拉指南第 {serial} 项未确认，已保留页面，未点击猜测的副本')


def select_book_target(task, serial, total, from_end=False):
    for attempt in range(3):
        try:
            row = _locate_book_target(task, serial, total, from_end)
            break
        except RuntimeError:
            if attempt == 2:
                raise
            task.sleep(.5)
    position = f'末尾第 {serial} 项' if from_end else f'第 {serial} 项'
    task.info_set('目标副本', f'索拉指南{position} · {row.title}')
    task.click(row.button, after_sleep=.6)
    from dualpc.navigation import BOOK_TRANSITION_FEATURES
    feature = task.wait_feature(BOOK_TRANSITION_FEATURES, time_out=10,
                                settle_time=.5, raise_if_not_found=True)
    return feature.name in ('team_start_challenge', 'team_close')
