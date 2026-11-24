import json
from types import SimpleNamespace
from unittest.mock import Mock
import cv2
import numpy as np
import pytest

from ok.util.image_io import imread, imwrite


def test_image_codecs_use_unicode_file_handles(tmp_path):
    folder = tmp_path / '中文目录 用户 数据'
    folder.mkdir()
    image = np.arange(32 * 24 * 3, dtype=np.uint8).reshape(24, 32, 3)
    path = folder / '角色 模板.png'
    assert imwrite(path, image)
    assert np.array_equal(imread(path), image)
    baseline = tmp_path / 'ascii.png'
    assert cv2.imwrite(str(baseline), image)
    assert np.array_equal(imread(path, cv2.IMREAD_GRAYSCALE), cv2.imread(str(baseline), cv2.IMREAD_GRAYSCALE))
    assert imread(folder / '缺少.png') is None
    (folder / '空.png').write_bytes(b'')
    assert imread(folder / '空.png') is None
    assert not imwrite(folder / '缺少文件夹' / '模板.png', image)


def test_feature_loader_reads_coco_images_under_a_unicode_install(tmp_path):
    from ok.feature.FeatureSet import read_from_json

    folder = tmp_path / '鸣潮脚本 安装目录'
    folder.mkdir()
    image = np.full((30, 40, 3), 91, np.uint8)
    assert imwrite(folder / '图片.png', image)
    coco = dict(images=[dict(id=1, file_name='图片.png', width=40, height=30)],
                categories=[dict(id=1, name='test_marker')],
                annotations=[dict(id=1, image_id=1, category_id=1, bbox=[4, 6, 12, 10])])
    annotation = folder / 'coco_annotations.json'
    annotation.write_text(json.dumps(coco, ensure_ascii=False), encoding='utf-8')
    features, boxes, *_ = read_from_json(str(annotation))
    assert features['test_marker'].mat.shape == (10, 12, 3)
    assert (features['test_marker'].mat == 91).all()
    assert boxes['test_marker'].width == 12


def test_template_store_saves_and_reads_unicode_profile_images(tmp_path):
    from ok.core.template_store import CocoTemplateStore

    store = CocoTemplateStore(tmp_path / '中文账号 图片模板')
    image = np.full((30, 40, 3), 155, np.uint8)
    path = store.save_frame(image)
    assert np.array_equal(imread(path), image)
    saved = store.load()['images'][0]
    assert saved['width'] == 40 and saved['height'] == 30


def test_unicode_install_is_opt_in_and_native_bridge_can_initialize(monkeypatch):
    import importlib
    import ok

    class ExecutorReached(Exception):
        pass

    monkeypatch.setattr(importlib.import_module('ok.util.file'), 'install_path_isascii',
                        lambda: (False, '中文安装目录'))
    monkeypatch.setattr(importlib.import_module('ok.core.screenshot'), 'Screenshot', Mock())
    executor = Mock(side_effect=ExecutorReached)
    monkeypatch.setattr(importlib.import_module('ok.task.TaskExecutor'), 'TaskExecutor', executor)
    runtime = SimpleNamespace(config={'ocr': {}}, exit_event=None, debug=False, feature_set=None,
                              device_manager=None, global_config=None, init_error=False,
                              init_device_manager=lambda: None,
                              should_init_task_manager_headless=lambda: True)
    runtime.config['ocr'] = {'target_height': 0}
    with pytest.raises(RuntimeError, match='English path'):
        ok.OK.do_init(runtime)
    executor.assert_not_called()
    runtime.config['allow_unicode_install_path'] = True
    with pytest.raises(ExecutorReached):
        ok.OK.do_init(runtime)
    executor.assert_called_once()
