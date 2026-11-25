"""Activity OCR reuses the host frame and model; standalone tasks keep SDK OCR."""
import re


class NativeActivity:
    def ocr(self, x=0, y=0, to_x=1, to_y=1, *, match=None, threshold=.2, **kwargs):
        interaction = self.executor.interaction
        channel = getattr(interaction, 'native_channel', None)
        if channel is None:
            return super().ocr(x, y, to_x, to_y, match=match, threshold=threshold, **kwargs)
        box = kwargs.pop('box', None)
        width, height = kwargs.pop('width', 0), kwargs.pop('height', 0)
        kwargs.pop('name', None)
        kwargs.pop('log', False)
        kwargs.pop('screenshot', False)  # The host records task diagnostics.
        kwargs.pop('target_height', 0)  # The host applies its OCR scaling.
        lib = kwargs.pop('lib', 'default')
        unsupported = [key for key, value in kwargs.items() if value is not None and value is not False]
        if unsupported or lib != 'default':
            raise ValueError(f'原生活动 OCR 不支持参数：{", ".join(unsupported or ["lib"])}')
        if box is not None or width or height:
            if box is None:
                box = self.box_of_screen(x, y, to_x, to_y, width, height)
            if isinstance(box, str):
                box = self.get_box_by_name(box)
            x, y, to_x, to_y = (box.x / self.width, box.y / self.height,
                                (box.x + box.width) / self.width, (box.y + box.height) / self.height)
        from ok import Box
        response = channel.request('activity_ocr', x=x, y=y, to_x=to_x, to_y=to_y,
                                   threshold=threshold or .2)
        boxes = [Box(*line['box'], name=line['text'], confidence=line['confidence'])
                 for line in response['lines']]
        if match is None:
            return boxes
        patterns = match if isinstance(match, (list, tuple)) else [match]
        return [box for box in boxes if any(
            pattern.search(box.name) if hasattr(pattern, 'search') else str(pattern) in box.name
            for pattern in patterns)]
