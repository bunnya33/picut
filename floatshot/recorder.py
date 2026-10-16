"""Bounded-memory screen recording with WASAPI tracks and an explicit pause clock."""
from __future__ import annotations

import math
import shutil
import subprocess
import threading
import time
import uuid
import wave
from dataclasses import dataclass
from pathlib import Path

import imageio_ffmpeg
import mss
import numpy as np
from PySide6.QtCore import QObject, Qt, Signal
from PySide6.QtGui import QImage

from .native import WINDOWS, composite_cursor

SAMPLE_RATE = 48000
CREATE_FLAGS = subprocess.CREATE_NO_WINDOW if WINDOWS else 0


class ActiveClock:
    def __init__(self, now=time.perf_counter):
        self.now = now
        self.lock = threading.RLock()
        self.started_at = None
        self.paused_at = None
        self.paused_total = 0.0
        self.finished_at = None

    def start(self):
        with self.lock:
            self.started_at = self.now()

    @property
    def paused(self):
        with self.lock:
            return self.paused_at is not None

    def pause(self):
        with self.lock:
            if self.started_at is not None and self.paused_at is None and self.finished_at is None:
                self.paused_at = self.now()

    def resume(self):
        with self.lock:
            if self.paused_at is not None:
                self.paused_total += self.now()-self.paused_at
                self.paused_at = None

    def elapsed(self):
        with self.lock:
            if self.started_at is None:
                return 0.0
            end = self.finished_at if self.finished_at is not None else self.paused_at
            if end is None:
                end = self.now()
            return max(0.0, end-self.started_at-self.paused_total)

    def finish(self):
        with self.lock:
            if self.finished_at is None:
                self.finished_at = self.paused_at if self.paused_at is not None else self.now()


@dataclass
class RecordOptions:
    region: dict
    output: Path
    fps: int = 30
    quality: int = 22
    cursor: bool = True
    system_audio: bool = False
    microphone: bool = False
    speaker_id: str = ""
    mic_id: str = ""


def output_dimensions(width, height):
    ratio = min(1.0, 1920/width, 1080/height)
    return max(2, int(width*ratio)//2*2), max(2, int(height*ratio)//2*2)


def audio_devices():
    import soundcard as sc
    speakers = sc.all_speakers()
    microphones = sc.all_microphones()
    return [(s.id, s.name) for s in speakers], [(m.id, m.name) for m in microphones]


def resolve_audio(options):
    if not options.system_audio and not options.microphone:
        return []
    import soundcard as sc
    result = []
    if options.system_audio:
        speaker = sc.get_speaker(options.speaker_id) if options.speaker_id else sc.default_speaker()
        if speaker is None:
            raise RuntimeError("找不到系统声音输出设备，请选择设备或关闭系统声音。")
        result.append(("system", speaker.id))
    if options.microphone:
        microphone = sc.get_microphone(options.mic_id) if options.mic_id else sc.default_microphone()
        if microphone is None:
            raise RuntimeError("找不到麦克风，请连接麦克风或关闭麦克风录制。")
        result.append(("mic", microphone.id))
    return result


class AudioTrack(threading.Thread):
    def __init__(self, kind, device_id, path, clock, started, stopped, warning):
        super().__init__(daemon=True, name=f"Audio-{kind}")
        self.kind, self.device_id, self.path = kind, device_id, path
        self.clock, self.started, self.stopped = clock, started, stopped
        self.warning = warning
        self.ready = threading.Event()
        self.muted = threading.Event()
        self.error = None
        self.level = 0.0

    def run(self):
        # COM objects and WASAPI streams must be created on their owning thread.
        import ctypes
        if WINDOWS:
            ctypes.windll.ole32.CoInitializeEx(None, 0)
        try:
            import soundcard as sc
            device = sc.get_microphone(self.device_id, include_loopback=self.kind == "system")
            with device.recorder(samplerate=SAMPLE_RATE, blocksize=960) as stream, wave.open(str(self.path), "wb") as wav:
                wav.setnchannels(2)
                wav.setsampwidth(2)
                wav.setframerate(SAMPLE_RATE)
                self.ready.set()
                written = 0
                while not self.stopped.is_set():
                    # record(None) returns available packets and avoids a blocking wait on silent loopback devices.
                    data = stream.record(numframes=None)
                    if data is None or len(data) == 0:
                        self.stopped.wait(.005)
                        continue
                    if not self.started.is_set() or self.clock.paused:
                        self.level = 0.0
                        continue
                    if data.ndim == 1:
                        data = data[:, None]
                    if data.shape[1] == 1:
                        data = np.repeat(data, 2, axis=1)
                    else:
                        data = data[:, :2]
                    self.level = min(1.0, float(np.sqrt(np.mean(data*data)))*4)
                    if self.muted.is_set():
                        data = np.zeros_like(data)
                    end = round(self.clock.elapsed()*SAMPLE_RATE)
                    if end < len(data):
                        data = data[-end:] if end else data[:0]
                    if not len(data):
                        continue
                    start = max(0, end-len(data))
                    # Place every packet on the same active timeline as the video; fill discontinuities with silence.
                    gap = start-written
                    while gap > 0:
                        chunk = min(gap, SAMPLE_RATE)
                        wav.writeframesraw(bytes(chunk*4))
                        written += chunk
                        gap -= chunk
                    skip = max(0, written-start)
                    data = data[skip:]
                    if len(data):
                        pcm = (np.clip(data, -1, 1)*32767).astype("<i2")
                        wav.writeframesraw(pcm.tobytes())
                        written += len(data)
                target = round(self.clock.elapsed()*SAMPLE_RATE)
                while written < target:
                    chunk = min(SAMPLE_RATE, target-written)
                    wav.writeframesraw(bytes(chunk*4))
                    written += chunk
        except Exception as exc:
            self.error = str(exc)
            self.ready.set()
            self.warning(f"{'系统声音' if self.kind == 'system' else '麦克风'}设备不可用：{exc}")
        finally:
            if WINDOWS:
                ctypes.windll.ole32.CoUninitialize()


def mux_tracks(video: Path, tracks: list[Path], output: Path, duration: float, log: Path):
    """Mux aligned WAV tracks, trim/pad to video duration, and atomically publish the MP4."""
    staging = output.with_name(output.stem+".part.mp4")
    if output.exists():
        raise RuntimeError("目标文件已存在，请另选保存路径；原始录制已保留。")
    if not tracks:
        shutil.copyfile(video, staging)
    else:
        command = [imageio_ffmpeg.get_ffmpeg_exe(), "-y", "-hide_banner", "-i", str(video)]
        for track in tracks:
            command += ["-i", str(track)]
        filters = []
        for index in range(len(tracks)):
            filters.append(f"[{index+1}:a]apad,atrim=duration={duration:.6f},asetpts=PTS-STARTPTS[a{index}]")
        if len(tracks) == 1:
            audio = "[a0]"
        else:
            inputs = "".join(f"[a{i}]" for i in range(len(tracks)))
            filters.append(f"{inputs}amix=inputs={len(tracks)}:normalize=0:duration=longest,alimiter=limit=0.95:latency=1[mixed]")
            audio = "[mixed]"
        command += ["-filter_complex", ";".join(filters), "-map", "0:v:0", "-map", audio,
                    "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-t", f"{duration:.6f}",
                    "-movflags", "+faststart", str(staging)]
        with log.open("ab") as stderr:
            result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=stderr,
                                    creationflags=CREATE_FLAGS, timeout=180)
        if result.returncode:
            raise RuntimeError(f"声音合成失败，原始录制已保留在 {video.parent}")
    staging.replace(output)


class Recorder(QObject):
    started = Signal()
    status = Signal(str)
    warning = Signal(str)
    finished = Signal(dict)
    failed = Signal(str)

    def __init__(self, temp_root: Path, cache_root: Path):
        super().__init__()
        self.temp_root, self.cache_root = temp_root, cache_root
        self.state = "idle"
        self.options = None
        self.clock = ActiveClock()
        self.stopped = threading.Event()
        self.begun = threading.Event()
        self.tracks = []
        self.thread = None

    @property
    def active(self):
        return self.state != "idle"

    def start(self, options: RecordOptions, frame_source=None, audio_sources=None):
        if self.active:
            raise RuntimeError("已有录制正在进行。")
        options.output.parent.mkdir(parents=True, exist_ok=True)
        if options.output.exists():
            raise RuntimeError("录屏目标文件已存在，请更换文件名。")
        if shutil.disk_usage(options.output.parent).free < 256*1024*1024:
            raise RuntimeError("保存磁盘可用空间不足 256 MB，请更换目录。")
        probe = options.output.parent / f".floatshot-{uuid.uuid4().hex}"
        probe.write_bytes(b"")
        probe.unlink()
        sources = resolve_audio(options) if audio_sources is None else audio_sources
        self.state = "starting"
        self.options = options
        self.clock = ActiveClock()
        self.stopped = threading.Event()
        self.begun = threading.Event()
        self.tracks = []
        self.thread = threading.Thread(target=self._run, args=(frame_source, sources), daemon=True, name="ScreenRecorder")
        self.thread.start()

    def pause(self):
        if self.state == "recording":
            self.clock.pause()
            self.state = "paused"
        elif self.state == "paused":
            self.clock.resume()
            self.state = "recording"

    def stop(self):
        if self.active and self.state != "finalizing":
            self.clock.finish()
            self.stopped.set()
            self.state = "finalizing"
            self.status.emit("正在生成视频…")

    def set_muted(self, kind, muted):
        for track in self.tracks:
            if track.kind == kind:
                track.muted.set() if muted else track.muted.clear()

    def _run(self, frame_source, sources):
        work = self.temp_root / uuid.uuid4().hex
        work.mkdir(parents=True)
        video = work / "screen.mp4"
        log = work / "ffmpeg.log"
        process = None
        grabber = None
        try:
            options = self.options
            w, h = options.region["width"], options.region["height"]
            out_w, out_h = output_dimensions(w, h)
            for kind, device_id in sources:
                track = AudioTrack(kind, device_id, work / f"{kind}.wav", self.clock,
                                   self.begun, self.stopped, self.warning.emit)
                self.tracks.append(track)
                track.start()
            for track in self.tracks:
                if not track.ready.wait(8) or track.error:
                    raise RuntimeError("声音设备初始化失败，请检查设备或关闭相应声音开关。")
            if self.stopped.is_set():
                raise RuntimeError("录制已取消。")
            if frame_source is None:
                grabber = mss.MSS()
                def frame_source():
                    shot = grabber.grab(options.region)
                    frame = np.frombuffer(shot.raw, np.uint8).reshape(h, w, 4).copy()
                    if options.cursor:
                        composite_cursor(frame, options.region)
                    return frame.tobytes()
            command = [imageio_ffmpeg.get_ffmpeg_exe(), "-y", "-hide_banner", "-loglevel", "warning",
                       "-f", "rawvideo", "-pixel_format", "bgra", "-video_size", f"{w}x{h}",
                       "-framerate", str(options.fps), "-i", "pipe:0", "-an",
                       "-vf", f"scale={out_w}:{out_h}", "-c:v", "libx264", "-preset", "veryfast",
                       "-crf", str(options.quality), "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(video)]
            with log.open("wb") as stderr:
                process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                                           stderr=stderr, creationflags=CREATE_FLAGS)
                first = frame_source()
                if len(first) != w*h*4:
                    raise RuntimeError("屏幕数据尺寸发生变化，请重新框选录制区域。")
                thumbnail = self.cache_root / f"{uuid.uuid4().hex}.png"
                QImage(first, w, h, w*4, QImage.Format_ARGB32).scaled(384, 216, Qt.KeepAspectRatio, Qt.SmoothTransformation).save(str(thumbnail))
                frames, last_frame = 0, first
                self.clock.start()
                self.state = "recording"
                self.begun.set()
                self.started.emit()
                last_disk_check = 0.0
                while not self.stopped.is_set():
                    if self.clock.paused:
                        self.stopped.wait(.02)
                        continue
                    now = self.clock.elapsed()
                    target = int(now*options.fps)+1
                    if target > frames:
                        try:
                            frame = frame_source()
                        except Exception as exc:
                            self.warning.emit(f"屏幕采集中断，正在保存已录内容：{exc}")
                            self.stop()
                            break
                        if len(frame) != w*h*4:
                            self.warning.emit("显示器尺寸改变，录制已停止。")
                            self.stop()
                            break
                        # Duplicate the last frame when capture is late, keeping the audio/video timeline aligned.
                        for _ in range(min(120, target-frames-1)):
                            process.stdin.write(last_frame)
                            frames += 1
                        process.stdin.write(frame)
                        last_frame = frame
                        frames += 1
                    if now-last_disk_check > 2:
                        last_disk_check = now
                        if shutil.disk_usage(work).free < 128*1024*1024 or shutil.disk_usage(options.output.parent).free < 128*1024*1024:
                            self.warning.emit("磁盘空间不足，录制已停止并尝试保存。")
                            self.stop()
                    self.stopped.wait(max(.001, min(.01, frames/options.fps-self.clock.elapsed())))
                target = max(1, math.ceil(self.clock.elapsed()*options.fps))
                while frames < target:
                    process.stdin.write(last_frame)
                    frames += 1
                process.stdin.close()
                process.wait(timeout=120)
            if process.returncode:
                raise RuntimeError(f"视频编码失败，诊断文件位于 {log}")
            for track in self.tracks:
                track.join(timeout=5)
                if track.is_alive():
                    raise RuntimeError(f"声音设备未正常停止，录制文件保留在 {work}")
            duration = frames/options.fps
            self.state = "finalizing"
            self.status.emit("正在合成声音…")
            track_paths = [track.path for track in self.tracks if track.path.exists() and track.path.stat().st_size > 44]
            mux_tracks(video, track_paths, options.output, duration, log)
            for temporary in work.iterdir():
                temporary.unlink()
            work.rmdir()
            self.state = "idle"
            self.finished.emit({"path": str(options.output), "duration": duration, "width": out_w,
                                "height": out_h, "thumbnail": str(thumbnail), "frames": frames})
        except Exception as exc:
            self.stopped.set()
            for track in self.tracks:
                track.join(timeout=2)
            if process and process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            self.state = "idle"
            self.failed.emit(f"{exc}\n\n如有录制片段，保留在：{work}")
        finally:
            if grabber:
                grabber.close()
