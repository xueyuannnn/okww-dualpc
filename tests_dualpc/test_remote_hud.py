from types import SimpleNamespace
from unittest.mock import Mock

import pytest

from workshop.bridge.native_io import native_party_hud
from src.task.BaseWWTask import BaseWWTask
from src.combat.CombatCheck import CombatCheck


@pytest.mark.parametrize('current', [-1, 0, 1, 2])
def test_native_hud_keeps_every_role_and_an_explicit_unknown(current):
    assert native_party_hud({'party_hud': {'visible': True, 'current': current, 'size': 3}}) == (True, current, 3)


@pytest.mark.parametrize('hud', [None, {'visible': 1, 'current': 0, 'size': 3},
                               {'visible': True, 'current': 3, 'size': 3},
                               {'visible': True, 'current': False, 'size': 3}])
def test_bad_host_hud_is_not_silently_used(hud):
    if hud is None:
        assert native_party_hud({}) is None
    else:
        with pytest.raises(ValueError):
            native_party_hud({'party_hud': hud})


@pytest.mark.parametrize('current', [-1, 0, 1, 2])
def test_workshop_uses_same_frame_host_hud_without_template_rescans(current):
    reader = SimpleNamespace(native_party_hud_supported=True, native_party_hud=(True, current, 3))
    task = SimpleNamespace(executor=SimpleNamespace(device_manager=SimpleNamespace(
        capture_method=SimpleNamespace(reader=reader))), frame=object(), find_one=Mock())
    assert BaseWWTask.in_team(task) == (True, current, 3)
    task.find_one.assert_not_called()


def test_workshop_does_not_keep_attacking_when_host_hud_disappears():
    reader = SimpleNamespace(native_party_hud_supported=True, native_party_hud=(False, -1, 0))
    task = SimpleNamespace(executor=SimpleNamespace(device_manager=SimpleNamespace(
        capture_method=SimpleNamespace(reader=reader))), frame=object(), find_one=Mock())
    assert BaseWWTask.in_team(task) == (False, -1, 0)
    task.find_one.assert_not_called()


@pytest.mark.parametrize('desktop,age,finished,expected', [
    (True, .2, False, True), (True, 1.1, False, False),
    (True, .2, True, False), (False, .2, False, False),
])
def test_workshop_remote_combat_hold_is_bounded_and_end_conditions_win(monkeypatch, desktop, age, finished, expected):
    monkeypatch.setattr('src.combat.CombatCheck.time.monotonic', lambda: 100.)
    task = SimpleNamespace(in_liberation=False, _in_combat=True,
        scene=SimpleNamespace(in_combat=lambda: None, set_in_combat=Mock(return_value=True)),
        check_f_break=Mock(), get_current_char=lambda: None, on_combat_check=lambda: True,
        has_target=lambda: False, is_streamed_image=lambda: desktop, check_health_bar=lambda: False,
        is_cloud_game=lambda: False, _native_vision={'desktop': desktop},
        _cloud_last_health_seen=100.-age, combat_end_condition=lambda: finished,
        reset_to_false=Mock(return_value=False), target_enemy=Mock(return_value=False),
        should_check_monthly_card=lambda: False)
    assert CombatCheck.do_check_in_combat(task, False) is expected
    if expected or finished:
        task.target_enemy.assert_not_called()
