"""Resumable, range-checked download of the two pinned public model weights."""
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import hashlib
import time
import urllib.request
from convert_translation_models import MODELS

ROOT = Path(__file__).resolve().parents[1] / "build" / "translation-sdk" / "original-models"
SIZE = 312087009
CHUNK = 4 * 1024 * 1024


def part(direction, repo, revision, start, download_url=None, file_size=SIZE):
    end = min(start + CHUNK, file_size) - 1
    path = ROOT / direction / "download-parts" / str(start)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.stat().st_size == end - start + 1:
        return
    error = None
    for attempt in range(8):
        try:
            request = urllib.request.Request(
                download_url or f"https://huggingface.co/{repo}/resolve/{revision}/pytorch_model.bin?download=true",
                headers={"Range": f"bytes={start}-{end}"},
            )
            with urllib.request.urlopen(request, timeout=45) as response:
                if response.status != 206 or response.headers.get("Content-Range") != f"bytes {start}-{end}/{file_size}":
                    raise RuntimeError(f"Unexpected range response: {response.status}, {response.headers.get('Content-Range')}")
                data = response.read(end - start + 2)
            if len(data) != end - start + 1:
                raise RuntimeError(f"Incomplete range: {len(data)} bytes")
            temporary = path.with_suffix(".partial")
            temporary.write_bytes(data)
            temporary.replace(path)
            return
        except Exception as exc:
            error = exc
            time.sleep(min(attempt + 1, 3))
    raise RuntimeError(f"{direction}/{start}: {error}")


def main():
    jobs = []
    for direction, (repo, revision, _, expected) in MODELS.items():
        folder = ROOT / direction
        folder.mkdir(parents=True, exist_ok=True)
        final = folder / "pytorch_model.bin"
        if final.exists():
            with final.open("rb") as stream:
                if hashlib.file_digest(stream, "sha256").hexdigest() == expected:
                    continue
        partial = folder / "pytorch_model.bin.partial"
        if partial.exists():
            parts = folder / "download-parts"
            parts.mkdir(exist_ok=True)
            with partial.open("rb") as stream:
                for start in range(0, partial.stat().st_size - CHUNK + 1, CHUNK):
                    (parts / str(start)).write_bytes(stream.read(CHUNK))
        jobs.extend((direction, repo, revision, start) for start in range(0, SIZE, CHUNK))
    with ThreadPoolExecutor(max_workers=16) as executor:
        futures = [executor.submit(part, *job) for job in jobs]
        for done, future in enumerate(as_completed(futures), 1):
            future.result()
            if done % 10 == 0 or done == len(jobs):
                print(f"Downloaded {done}/{len(jobs)} checked ranges", flush=True)
    for direction, (_, _, _, expected) in MODELS.items():
        folder = ROOT / direction
        final = folder / "pytorch_model.bin"
        if final.exists():
            with final.open("rb") as stream:
                if hashlib.file_digest(stream, "sha256").hexdigest() == expected:
                    continue
        assembled = folder / "pytorch_model.bin.assembled"
        with assembled.open("wb") as output:
            for start in range(0, SIZE, CHUNK):
                output.write((folder / "download-parts" / str(start)).read_bytes())
        with assembled.open("rb") as stream:
            actual = hashlib.file_digest(stream, "sha256").hexdigest()
        if actual != expected:
            raise RuntimeError(f"Model checksum mismatch: {direction}")
        assembled.replace(final)
        print(f"Verified {direction}: {actual}", flush=True)


if __name__ == "__main__":
    main()
