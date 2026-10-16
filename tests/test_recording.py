import re
import subprocess
import time
import wave
from pathlib import Path

import imageio_ffmpeg
import numpy as np
import pytest

from floatshot.recorder import (ActiveClock, CREATE_FLAGS, RecordOptions, Recorder,
                                mux_tracks, output_dimensions)


def inspect_video(path):
    result = subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-i", str(path), "-f", "null", "-"],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=CREATE_FLAGS)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    return result.stderr.decode(errors="replace")


def test_pause_clock_excludes_pause_and_freezes_after_stop():
    current = [0.0]
    clock = ActiveClock(lambda: current[0])
    clock.start()
    current[0] = 2
    clock.pause()
    current[0] = 17
    assert clock.elapsed() == 2
    clock.resume()
    current[0] = 20
    assert clock.elapsed() == 5
    clock.finish()
    current[0] = 90
    assert clock.elapsed() == 5


def test_stop_while_paused():
    current = [10.0]
    clock = ActiveClock(lambda: current[0])
    clock.start()
    current[0] = 11
    clock.pause()
    current[0] = 30
    clock.finish()
    assert clock.elapsed() == 1


@pytest.mark.parametrize("size,expected", [((3840, 2160), (1920, 1080)), ((301, 199), (300, 198)),
                                         ((800, 1400), (616, 1080)), ((80, 60), (80, 60))])
def test_output_dimensions(size, expected):
    assert output_dimensions(*size) == expected


def record_sample(app, tmp_path):
    recorder = Recorder(tmp_path / "temp", tmp_path / "cache")
    recorder.temp_root.mkdir()
    recorder.cache_root.mkdir()
    frame = np.zeros((90, 160, 4), dtype=np.uint8)
    frame[:, :, 1] = 160
    frame[:, :, 3] = 255
    recorder.start(RecordOptions({"left": 0, "top": 0, "width": 160, "height": 90},
                                 tmp_path / "sample.mp4", fps=15, cursor=False),
                   frame_source=lambda: frame.tobytes(), audio_sources=[])
    deadline = time.monotonic()+10
    while recorder.state == "starting" and time.monotonic() < deadline:
        app.processEvents()
        time.sleep(.01)
    assert recorder.state == "recording"
    time.sleep(.35)
    recorder.pause()
    paused_at = recorder.clock.elapsed()
    time.sleep(.4)
    assert recorder.clock.elapsed() == paused_at
    recorder.pause()
    time.sleep(.35)
    recorder.stop()
    recorder.thread.join(timeout=20)
    app.processEvents()
    assert not recorder.thread.is_alive()
    assert (tmp_path / "sample.mp4").is_file()
    return tmp_path / "sample.mp4", recorder


def test_real_ffmpeg_encode_and_pause(app, tmp_path):
    path, recorder = record_sample(app, tmp_path)
    info = inspect_video(path)
    match = re.search(r"Duration: (\d+):(\d+):([\d.]+)", info)
    duration = int(match[1])*3600+int(match[2])*60+float(match[3])
    assert .65 <= duration <= 1.1
    assert "h264" in info
    assert "160x90" in info
    assert recorder.state == "idle"
    assert not list((tmp_path / "temp").iterdir())


def test_two_audio_tracks_are_mixed_and_decodable(app, tmp_path):
    video, recorder = record_sample(app, tmp_path)
    paths = []
    for index, frequency in enumerate((440, 880)):
        path = tmp_path / f"tone-{index}.wav"
        samples = np.sin(np.arange(48000)*2*np.pi*frequency/48000)*.1
        stereo = np.repeat(samples[:, None], 2, axis=1)
        with wave.open(str(path), "wb") as output:
            output.setnchannels(2)
            output.setsampwidth(2)
            output.setframerate(48000)
            output.writeframes((stereo*32767).astype("<i2").tobytes())
        paths.append(path)
    target = tmp_path / "mixed.mp4"
    mux_tracks(video, paths, target, .733333, tmp_path / "mux.log")
    info = inspect_video(target)
    assert "aac" in info
    decoded = subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-i", str(target), "-vn", "-f", "f32le",
                              "-ac", "1", "-ar", "48000", "pipe:1"],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, creationflags=CREATE_FLAGS)
    data = np.frombuffer(decoded.stdout, np.float32)
    fft = np.abs(np.fft.rfft(data[:24000]))
    assert fft[220] > 200
    assert fft[440] > 200


def test_second_recording_is_rejected(app, tmp_path):
    recorder = Recorder(tmp_path, tmp_path)
    recorder.state = "recording"
    with pytest.raises(RuntimeError, match="已有录制"):
        recorder.start(RecordOptions({}, tmp_path / "never.mp4"))
