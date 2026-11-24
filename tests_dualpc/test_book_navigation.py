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


def test_stationary_page_tolerates_ocr_typos_but_scroll_matching_does_not():
    previous = [row(n) for n in range(1, 5)]
    current = [row(n) for n in range(1, 5)]
    for item in current:
        item.title = item.title.replace('Boss', 'B0ss')
    assert book.same_page(previous, current)
    assert book.overlap(previous, current) is None
    current[0].y = .02
    assert not book.same_page(previous, current)


def test_missing_description_and_ocr_order_do_not_change_row_identity():
    left = row(1)
    right = row(1)
    left.title = '山麓回音|声骸奖励'
    right.title = '山麓回音'
    assert book.similarity(left, right) >= .86
    right.title = '声骸奖励|山麓回音'
    assert book.similarity(left, right) >= .86
    right.title = '另一处无音区|声骸奖励'
    assert book.similarity(left, right) == 0


@pytest.mark.parametrize('label', ['前往', '直接挑战', '直接挑戰', 'Direct Challenge'])
def test_buttons_left_of_old_search_boundary_are_read_without_template(label):
    button = SimpleNamespace(x=1090, y=345, width=90, height=30, text=label, confidence=.99)
    name = SimpleNamespace(x=600, y=310, width=180, height=25, text='沉心域无音区', confidence=.99)
    task = SimpleNamespace(frame=np.zeros((720, 1280, 3), np.uint8), box_of_screen=Mock(),
                           find_feature=Mock(return_value=[]), ocr=Mock(return_value=[name, button]))
    rows = book.read_rows(task)
    assert len(rows) == 1
    assert rows[0].button is button
    assert rows[0].title == name.text


def test_group_header_edge_is_not_counted_as_a_proceed_button():
    edge = SimpleNamespace(x=1215, y=590, width=20, height=40, confidence=.75)
    task = SimpleNamespace(frame=np.full((720, 1280, 3), 180, np.uint8), box_of_screen=Mock(),
                           find_feature=Mock(return_value=[edge]), ocr=Mock(return_value=[]))
    assert book.read_rows(task) == []


def test_top_clipped_title_can_use_two_other_stationary_rows():
    previous = [row(n) for n in range(1, 4)]
    current = [row(n) for n in range(1, 4)]
    for page in [previous, current]:
        for i, item in enumerate(page):
            item.y = .22 + i * .14
        page[0].identity[:] = 40
        page[0].title = ''
    assert book.same_page(previous, current)
    assert not book.same_page(previous[:2], current[:2])
    current[1].title = 'different destination'
    current[1].identity = row(99).identity
    assert not book.same_page(previous, current)


def test_scroll_waits_for_ocr_to_settle_before_scrolling_back(monkeypatch):
    position = [0]
    reads = [0]
    task = SimpleNamespace(sleep=Mock(), screenshot=Mock(), info_set=Mock(), click=Mock(),
                           wait_feature=Mock(return_value=SimpleNamespace(name='team_close')))
    def scroll(x, y, amount):
        position[0] = 0 if amount > 50 else max(0, min(17, position[0] + (1 if amount < 0 else -1)))
        reads[0] = 0
    task.scroll_relative = Mock(side_effect=scroll)
    def observe(_):
        reads[0] += 1
        page = [row(n) for n in range(position[0] + 1, position[0] + 5)]
        if position[0] and reads[0] == 1:
            for item in page:
                item.title = item.title.replace('Boss', 'B0ss')
        return page
    monkeypatch.setattr(book, 'read_rows', observe)
    assert book.select_book_target(task, 9, 21)
    assert task.click.call_args.args[0] == 9
    assert not any(call.args[2] == 1 for call in task.scroll_relative.call_args_list)


def test_top_reset_waits_out_rebound_without_restarting_the_scroll(monkeypatch):
    clock = [0.]
    reset_at = [-10.]
    task = SimpleNamespace(screenshot=Mock(), info_set=Mock(), click=Mock(),
                           wait_feature=Mock(return_value=SimpleNamespace(name='team_close')))
    task.sleep = lambda seconds: clock.__setitem__(0, clock[0] + seconds)
    task.scroll_relative = Mock(side_effect=lambda x, y, amount: reset_at.__setitem__(0, clock[0]))
    def observe(_):
        page = [row(n) for n in range(1, 5)]
        for i, item in enumerate(page):
            item.y = .33 + i * .14 + (.018 if clock[0] - reset_at[0] < .6 else 0.)
        return page
    monkeypatch.setattr(book, 'read_rows', observe)
    assert book.select_book_target(task, 2, 21)
    assert task.click.call_args.args[0] == 2
    assert task.scroll_relative.call_count == 2


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
