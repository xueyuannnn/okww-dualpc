from types import SimpleNamespace
from unittest.mock import Mock
import json
from pathlib import Path

import cv2
import numpy as np
import pytest

from dualpc.combat import desktop_health_bar, desktop_target_ring_evidence
from src.combat.CombatCheck import CombatCheck
from src.task.BaseWWTask import BaseWWTask


def target_template(name):
    root = Path(__file__).parents[1]
    data = json.loads((root / 'assets/coco_annotations.json').read_text('utf8'))
    category = next(c for c in data['categories'] if c['name'] == name)
    annotation = next(a for a in data['annotations'] if a['category_id'] == category['id'])
    image = next(i for i in data['images'] if i['id'] == annotation['image_id'])
    frame = cv2.imread(str(root / 'assets' / image['file_name']))
    x, y, w, h = annotation['bbox']
    return cv2.resize(frame[y:y + h, x:x + w], (30, 29))


@pytest.mark.parametrize('scale', [1, .67, 1.33])
@pytest.mark.parametrize('kind', ['locked', 'unlocked', 'golden-unlocked'])
def test_only_a_distinct_target_ring_is_lock_evidence(scale, kind):
    reference = target_template('has_target')
    image = (cv2.imread(str(Path(__file__).parent.parent / 'tests/fixtures/remote-target/golden-unlocked.png'))
             if kind == 'golden-unlocked' else target_template('has_target' if kind == 'locked' else 'no_target'))
    size = (round(30 * scale), round(29 * scale))
    reference, image = cv2.resize(reference, size), cv2.resize(image, size)
    image = cv2.imdecode(cv2.imencode('.jpg', image, [cv2.IMWRITE_JPEG_QUALITY, 45])[1], 1)
    assert desktop_target_ring_evidence(image, reference) is (kind == 'locked')


def test_compressed_enemy_bar_keeps_tolerance_inside_the_enemy_region():
    frame = np.zeros((1080, 1920, 3), np.uint8)
    frame[300:308, 500:620] = (60, 65, 190)
    frame[301:307, 501:619:2] = 0
    assert desktop_health_bar(frame)
    assert not desktop_health_bar(frame, relaxed=False)


@pytest.mark.parametrize('size', [(1280, 720), (1920, 1080), (2560, 1440)])
def test_player_bars_and_remote_overlays_do_not_extend_enemy_combat(size):
    width, height = size
    frame = np.zeros((height, width, 3), np.uint8)
    for x, y, w, h in ((.36, .95, .19, .006), (.89, .13, .05, .006), (.02, .06, .05, .006)):
        frame[round(y * height):round((y + h) * height), round(x * width):round((x + w) * width)] = (65, 70, 200)
    assert not desktop_health_bar(frame)
    cv2.rectangle(frame, (width // 3, height // 3),
                  (width // 3 + width // 12, height // 3 + height // 180), (65, 70, 200), -1)
    assert desktop_health_bar(frame)


@pytest.mark.parametrize('marker,locked', [('has_target', True), ('no_target', False)])
def test_displaced_remote_target_never_opens_a_menu(marker, locked):
    from ok import Box
    reference = target_template('has_target')
    frame = target_template(marker)
    box = SimpleNamespace(scale=lambda *args: None)
    task = SimpleNamespace(
        _native_vision={'desktop': True}, is_cloud_game=lambda: False, is_browser=lambda: False,
        get_target_names=lambda: ('has_target', 'no_target'), get_box_by_name=lambda name: box,
        allow_target_box_short_combat_check=lambda: False,
        find_best_match_in_box=Mock(side_effect=[None, None, None, Box(0,0,30,29,name=marker,confidence=.95)]),
        frame=frame, get_feature_by_name=lambda name: SimpleNamespace(mat=reference),
        esc_count=0, send_key=Mock(), sleep=Mock())
    assert bool(CombatCheck.has_target(task)) is locked
    task.send_key.assert_not_called()
    task.sleep.assert_not_called()


def test_explicit_remote_completion_precedes_rotation_skip_and_target():
    task = SimpleNamespace(in_liberation=False, _in_combat=True, _native_vision={'desktop': True},
        scene=SimpleNamespace(in_combat=lambda: None), combat_end_condition=lambda: True,
        reset_to_false=Mock(return_value=False), check_f_break=Mock(), has_target=Mock(return_value=True))
    assert not CombatCheck.do_check_in_combat(task, False)
    task.reset_to_false.assert_called_once_with(reason='end condition reached')
    task.has_target.assert_not_called()
    task.check_f_break.assert_not_called()


@pytest.mark.parametrize('desktop,team,menu,allowed', [
    (True, False, False, False), (True, False, True, True),
    (True, True, True, False), (False, False, False, True)])
def test_generic_python_navigation_requires_a_recognized_remote_menu(desktop, team, menu, allowed):
    reader = SimpleNamespace(native_desktop_capture=desktop)
    task = SimpleNamespace(frame=np.zeros((2, 2, 3), np.uint8),
        executor=SimpleNamespace(device_manager=SimpleNamespace(capture_method=SimpleNamespace(reader=reader))),
        in_team=lambda: (team, 0, 3), find_one=Mock(return_value=object() if menu else None))
    assert BaseWWTask.can_back_to_world(task) is allowed
    if not desktop or team:
        task.find_one.assert_not_called()
