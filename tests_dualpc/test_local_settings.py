import json
from dataclasses import asdict

import pytest

from dualpc.settings import Settings


@pytest.mark.parametrize('mode', ['auto', 'local', 'remote'])
@pytest.mark.parametrize('backend', ['DXGI', 'WIN32'])
def test_native_recognition_mode_loads_and_survives_python_save(tmp_path, mode, backend):
    value = asdict(Settings())
    value['capture'].update(source='desktop', local_backend=backend, recognition_mode=mode)
    value['kmbox'].update(protocol='sendinput', dry_run=False)
    path = tmp_path / 'dualpc.json'
    path.write_text(json.dumps(value), encoding='utf-8')
    loaded = Settings.load(path)
    assert loaded.capture.recognition_mode == ('local' if mode == 'auto' else mode)
    loaded.save(path)
    assert json.loads(path.read_text('utf-8'))['capture']['recognition_mode'] == ('local' if mode == 'auto' else mode)


def test_recognition_mode_has_only_explicit_choices_and_rejects_invalid_values():
    settings = Settings().validate()
    assert settings.capture.recognition_mode == 'local'
    settings.capture.recognition_mode = 'not-a-mode'
    with pytest.raises(ValueError, match='识别模式'):
        settings.validate()


@pytest.mark.parametrize('backend', ['DXGI', 'WIN32'])
def test_local_configuration_round_trips_without_box_credentials(tmp_path, backend):
    settings = Settings()
    settings.capture.source = 'desktop'
    settings.capture.local_backend = backend
    settings.capture.local_target = {'kind': 'window', 'title': 'test'}
    settings.capture.width = 0
    settings.capture.fourcc = 'P010'
    settings.kmbox.protocol = 'sendinput'
    settings.kmbox.dry_run = False
    settings.kmbox.target = {'kind': 'window', 'title': 'test'}
    path = tmp_path / 'local.json'
    settings.save(path)
    loaded = Settings.load(path)
    assert loaded == settings
    loaded.capture.dxgi_follow_refresh = False
    loaded.capture.local_fps = 540
    loaded.save(path)
    assert Settings.load(path) == loaded
    for fps in (0, 1001, 60.5):
        loaded.capture.local_fps = fps
        with pytest.raises(ValueError):
            loaded.validate()
    loaded.capture.local_fps = 60
    loaded.kmbox.target = {}
    loaded.validate()


def test_dxgi_and_sendinput_need_no_manual_target_or_frame_rate():
    settings = Settings()
    settings.capture.source = 'desktop'
    settings.capture.local_backend = 'DXGI'
    settings.kmbox.protocol = 'sendinput'
    settings.kmbox.dry_run = False
    settings.validate()
    assert settings.capture.dxgi_monitor == ''
    assert settings.capture.dxgi_follow_refresh
    assert settings.capture.local_target == {}
    assert settings.kmbox.target == {}


def test_legacy_default_still_uses_capture_card_and_kmbox():
    settings = Settings().validate()
    assert settings.capture.source == 'card'
    assert settings.kmbox.protocol == 'kmbox_net'


def test_win32_primary_monitor_and_optional_sendinput_window_round_trip(tmp_path):
    settings = Settings()
    settings.capture.source = 'desktop'
    settings.capture.local_backend = 'WIN32'
    settings.kmbox.protocol = 'sendinput'
    settings.kmbox.dry_run = False
    settings.validate()
    settings.kmbox.target_enabled = True
    with pytest.raises(ValueError):
        settings.validate()
    settings.kmbox.target = {'kind': 'window', 'title': 'test'}
    path = tmp_path / 'advanced.json'
    settings.save(path)
    assert Settings.load(path) == settings
    settings.kmbox.target_enabled = False
    settings.kmbox.target = {}
    settings.validate()
