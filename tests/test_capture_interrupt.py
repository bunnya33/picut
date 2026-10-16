import numpy as np

from floatshot.recorder import RecordOptions, Recorder
from test_recording import inspect_video


def test_capture_device_loss_still_saves_playable_partial_video(app, tmp_path):
    temp, cache = tmp_path / "temp", tmp_path / "cache"
    temp.mkdir()
    cache.mkdir()
    recorder = Recorder(temp, cache)
    frame = np.full((90, 160, 4), 128, dtype=np.uint8).tobytes()
    calls = [0]
    def source():
        calls[0] += 1
        if calls[0] > 3:
            raise OSError("simulated disconnected monitor")
        return frame
    target = tmp_path / "partial.mp4"
    recorder.start(RecordOptions({"left": 0, "top": 0, "width": 160, "height": 90}, target, fps=15),
                   frame_source=source, audio_sources=[])
    recorder.thread.join(timeout=15)
    app.processEvents()
    assert not recorder.thread.is_alive()
    assert "h264" in inspect_video(target)
    assert recorder.state == "idle"
