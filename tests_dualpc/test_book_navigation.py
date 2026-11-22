from types import SimpleNamespace
from unittest.mock import Mock

import cv2
import numpy as np
import pytest

from dualpc import book_navigation as book
from src.task.BaseWWTask import BaseWWTask


def row(index, title=True):
    random = np.random.default_rng(index)
    image = random.integers(0, 255, (48, 192), dtype=np.uint8)
    return book.BookRow(index, image, f'Boss{index}' if title else '')


@pytest.mark.parametrize('target', range(1, 22))
def test_absolute_selection_resets_previous_scroll_and_stitches_overlapping_pages(monkeypatch, target):
    position = [9]
    task = SimpleNamespace(sleep=Mock(), screenshot=Mock(), info_set=Mock(), click=Mock(),
                           wait_feature=Mock(return_value=SimpleNamespace(name='team_close')))
    def scroll(x, y, amount):
        assert (x, y) == (.68, .55)
        position[0] = 0 if amount > 50 else max(0, min(17, position[0] + (1 if amount < 0 else -1)))
    task.scroll_relative = scroll
    monkeypatch.setattr(book, 'read_rows', lambda _: [row(n) for n in range(position[0] + 1, position[0] + 5)])
    assert book.select_book_target(task, target, 21)
    assert task.click.call_args.args[0] == target


@pytest.mark.parametrize('target', [1, 2, 3])
def test_simulation_materials_are_selected_from_verified_bottom_rows(monkeypatch, target):
    task = SimpleNamespace(sleep=Mock(), screenshot=Mock(), info_set=Mock(), click=Mock(),
                           scroll_relative=Mock(), wait_feature=Mock(return_value=SimpleNamespace(name='team_close')))
    monkeypatch.setattr(book, 'read_rows', lambda _: [row(n) for n in range(18, 22)])
    assert book.select_book_target(task, target, 3, from_end=True)
    assert task.click.call_args.args[0] == 22 - target


def test_missing_overlap_does_not_click_an_unverified_last_row(monkeypatch):
    page = [0]
    task = SimpleNamespace(sleep=Mock(), screenshot=Mock(), info_set=Mock(), click=Mock(), wait_feature=Mock())
    task.scroll_relative = lambda x, y, count: page.__setitem__(0, 0 if count > 50 else page[0] + 1)
    monkeypatch.setattr(book, 'read_rows', lambda _: [row(n + page[0] * 10) for n in range(1, 5)])
    with pytest.raises(RuntimeError, match='未确认'):
        book.select_book_target(task, 8, 21)
    task.click.assert_not_called()
    assert task.screenshot.call_count == 3


def test_compressed_identity_and_clipped_first_row_keep_the_same_offset():
    previous = [row(n, False) for n in range(1, 5)]
    current = [row(n, False) for n in range(2, 6)]
    for item in current[1:]:
        _, encoded = cv2.imencode('.jpg', item.identity, [cv2.IMWRITE_JPEG_QUALITY, 35])
        item.identity = cv2.imdecode(encoded, cv2.IMREAD_GRAYSCALE)
    current[0] = row(100, False)  # The top identity crop crosses the viewport boundary.
    assert book.overlap(previous, current) == 1


def test_prepare_guide_keeps_an_existing_page_and_leaves_world_cleanup_explicit():
    task = SimpleNamespace(book_is_open=Mock(return_value=True), logged_in=False,
                           info_set=Mock(), wait_until=Mock(), sleep=Mock(), is_main=Mock())
    assert BaseWWTask.ensure_main(task, allow_book=True)
    task.wait_until.assert_not_called()
    BaseWWTask.ensure_main(task)
    task.wait_until.assert_called_once()


def test_open_guide_reuses_requested_tab_without_escape_or_hotkey():
    tab = object()
    task = SimpleNamespace(book_is_open=Mock(return_value=True), find_one=Mock(return_value=tab),
                           click_box=Mock(), ensure_main=Mock(), send_key=Mock())
    assert BaseWWTask.openF2Book(task, 'gray_book_boss') is tab
    task.ensure_main.assert_not_called()
    task.send_key.assert_not_called()
    task.click_box.assert_called_once_with(tab, after_sleep=.6)


@pytest.mark.parametrize('category', ['zhange', 'wuyin', 'qiangdi', 'ningsu', 'moni', 'canxiang', 'mengyan'])
def test_category_prefers_the_observed_label_to_a_fixed_row(category):
    label = object()
    task = SimpleNamespace(log_info=Mock(), ocr=Mock(return_value=[label]),
                           box_of_screen=Mock(return_value='sidebar'), click=Mock())
    BaseWWTask.open_boss_book(task, category)
    task.click.assert_called_once_with(label, after_sleep=2)
