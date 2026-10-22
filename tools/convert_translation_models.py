"""Build tool only. The shipped Windows application uses native C++ workers."""
import argparse
import hashlib
import json
import shutil
import urllib.request
import socket
import time
from pathlib import Path

MODELS = {
    "en-zh": ("Helsinki-NLP/opus-mt-en-zh", "408d9bc410a388e1d9aef112a2daba955b945255", "apache-2.0", "69a1d6ec829cee349360b3b677ac0aa99a7d88822d1a6578370029efffdca3f5"),
    "zh-en": ("Helsinki-NLP/opus-mt-zh-en", "cf109095479db38d6df799875e34039d4938aaa6", "cc-by-4.0", "9d8ceb91d103ef89400c9d9d62328b4858743cf8924878aee3b8afc594242ce0"),
}


def checksum(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest().upper()


def main():
    socket.setdefaulttimeout(45)
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--download-only", action="store_true")
    args = parser.parse_args()
    sdk = args.root / "build" / "translation-sdk"
    output = sdk / "models"
    output.mkdir(parents=True, exist_ok=True)
    manifest = {"schema": 1, "engine": "CTranslate2 4.6.0", "quantization": "int8", "models": {}}
    for direction, (repo, revision, license_id, weight_hash) in MODELS.items():
        raw = sdk / "original-models" / direction
        raw.mkdir(parents=True, exist_ok=True)
        for name in ("config.json", "pytorch_model.bin", "source.spm", "target.spm", "tokenizer_config.json", "vocab.json", "README.md"):
            path = raw / name
            if path.exists() and (name != "pytorch_model.bin" or checksum(path).lower() == weight_hash):
                continue
            print(f"Downloading {direction}/{name}", flush=True)
            temporary = path.with_suffix(path.suffix + ".partial")
            for attempt in range(5):
                try:
                    urllib.request.urlretrieve(f"https://huggingface.co/{repo}/resolve/{revision}/{name}", temporary)
                    break
                except Exception:
                    if attempt == 4:
                        raise
                    time.sleep(1)
            if name == "pytorch_model.bin" and checksum(temporary).lower() != weight_hash:
                raise RuntimeError(f"Upstream weight checksum mismatch: {direction}")
            temporary.replace(path)
        if args.download_only:
            continue
        destination = output / direction
        if not (destination / "model.bin").exists():
            from ctranslate2.converters import TransformersConverter
            print(f"Converting {direction} to native INT8", flush=True)
            TransformersConverter(str(raw)).convert(str(destination), quantization="int8", force=True)
        for name in ("source.spm", "target.spm", "README.md"):
            shutil.copy2(raw / name, destination / name)
        source, target = direction.split("-")
        manifest["models"][direction] = {
            "source": source, "target": target, "repository": repo,
            "revision": revision, "license": license_id,
            "url": f"https://huggingface.co/{repo}/tree/{revision}",
            "modifications": "Converted from original PyTorch weights to CTranslate2 INT8; tokenizer retained.",
            "checksums": {p.name: checksum(p) for p in sorted(destination.iterdir()) if p.is_file()},
        }
    if args.download_only:
        return
    (output / "models.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Offline translation pack ready: {output}", flush=True)


if __name__ == "__main__":
    main()
