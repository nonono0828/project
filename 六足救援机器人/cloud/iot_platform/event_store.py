"""Small durable event journal shared by all frontend sessions."""

import copy
import json
import math
import os
import tempfile
import threading
import time
from pathlib import Path


EVENT_TYPES = frozenset({
    "survivor", "gas", "modem", "wifi", "video", "location", "robot_control",
})
MAX_EVENT_ID_LENGTH = 128
MAX_DEDUPE_KEY_LENGTH = 256
MAX_TEXT_LENGTH = 512
# A validated JPEG may be up to 512 KiB. Base64 expands it by roughly 4/3,
# and the data-URL prefix adds a few more bytes.
MAX_SNAPSHOT_LENGTH = 750_000


def _text(value, limit=MAX_TEXT_LENGTH):
    if value is None:
        return ""
    return str(value).strip()[:limit]


def _finite(value):
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(value)
    )


def _location(value):
    if not isinstance(value, dict):
        return None
    lat = value.get("lat")
    lng = value.get("lng")
    if not (_finite(lat) and _finite(lng) and -90 <= lat <= 90 and -180 <= lng <= 180):
        return None
    result = {"lat": float(lat), "lng": float(lng)}
    for key in ("source", "accuracy"):
        if key == "source" and isinstance(value.get(key), str):
            result[key] = _text(value[key], 32)
        elif key == "accuracy" and _finite(value.get(key)):
            result[key] = float(value[key])
    return result


def normalize_event(raw):
    if not isinstance(raw, dict):
        return None
    event_type = raw.get("type")
    if event_type not in EVENT_TYPES:
        return None
    event_id = _text(raw.get("id"), MAX_EVENT_ID_LENGTH)
    if not event_id:
        return None
    timestamp = raw.get("timestamp")
    if not _finite(timestamp):
        timestamp = time.time() * 1000
    event = {
        "id": event_id,
        "type": event_type,
        "timestamp": int(timestamp),
        "deviceId": _text(raw.get("deviceId"), 128) or None,
        "bootId": _text(raw.get("bootId"), 128) or None,
        "sequence": int(raw["sequence"]) if _finite(raw.get("sequence")) else None,
        "dedupeKey": _text(raw.get("dedupeKey"), MAX_DEDUPE_KEY_LENGTH) or None,
        "success": raw.get("success") if isinstance(raw.get("success"), bool) else None,
        "operation": _text(raw.get("operation")) or None,
        "message": _text(raw.get("message")) or None,
        "location": _location(raw.get("location")),
        "gas": copy.deepcopy(raw.get("gas")) if isinstance(raw.get("gas"), dict) else None,
        "snapshotStatus": _text(raw.get("snapshotStatus"), 32) or "none",
        "snapshotData": None,
        "frameId": int(raw["frameId"]) if _finite(raw.get("frameId")) else None,
        "frameSequence": int(raw["frameSequence"])
        if _finite(raw.get("frameSequence")) else None,
    }
    snapshot_data = raw.get("snapshotData")
    if (
        isinstance(snapshot_data, str)
        and snapshot_data.startswith("data:image/")
        and len(snapshot_data) <= MAX_SNAPSHOT_LENGTH
    ):
        event["snapshotData"] = snapshot_data
        event["snapshotStatus"] = "ready"
    elif event["snapshotStatus"] == "ready":
        event["snapshotStatus"] = "error"
    return event


class EventJournal:
    def __init__(self, path=None, max_events=200):
        default_path = Path(__file__).resolve().with_name("events.json")
        self.path = Path(path or os.getenv("EVENT_JOURNAL_PATH", default_path))
        self.max_events = max_events
        self._lock = threading.RLock()
        self._events = []
        self._updated_at = 0
        self._load()

    def _load(self):
        try:
            payload = json.loads(self.path.read_text(encoding="utf-8"))
        except (FileNotFoundError, OSError, ValueError):
            return
        source = payload.get("events") if isinstance(payload, dict) else payload
        if not isinstance(source, list):
            return
        for item in source:
            event = normalize_event(item)
            if event is not None:
                self._events.append(event)
        self._events = self._events[: self.max_events]
        if isinstance(payload, dict) and _finite(payload.get("updatedAt")):
            self._updated_at = int(payload["updatedAt"])

    def _persist_locked(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._updated_at = int(time.time() * 1000)
        payload = {"updatedAt": self._updated_at, "events": self._events}
        fd, temp_name = tempfile.mkstemp(
            prefix=f".{self.path.name}.", suffix=".tmp", dir=str(self.path.parent)
        )
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as handle:
                json.dump(payload, handle, ensure_ascii=False, separators=(",", ":"))
                handle.write("\n")
            os.replace(temp_name, self.path)
        finally:
            if os.path.exists(temp_name):
                os.unlink(temp_name)

    def snapshot(self, limit=None):
        with self._lock:
            count = self.max_events if limit is None else max(1, min(int(limit), self.max_events))
            return {
                "updatedAt": self._updated_at,
                "events": copy.deepcopy(self._events[:count]),
            }

    def upsert(self, raw):
        event = normalize_event(raw)
        if event is None:
            raise ValueError("invalid_event")
        with self._lock:
            index = next(
                (i for i, item in enumerate(self._events) if item["id"] == event["id"]),
                None,
            )
            if event["dedupeKey"]:
                duplicate = next(
                    (item for item in self._events if item.get("dedupeKey") == event["dedupeKey"]),
                    None,
                )
                if duplicate is not None:
                    index = self._events.index(duplicate)
                    event["id"] = duplicate["id"]
            if index is None:
                self._events.insert(0, event)
            else:
                old = self._events[index]
                event["snapshotData"] = event["snapshotData"] or old.get("snapshotData")
                event["snapshotStatus"] = (
                    "ready" if event["snapshotData"] else event["snapshotStatus"]
                )
                self._events[index] = event
                if index:
                    self._events.insert(0, self._events.pop(index))
            del self._events[self.max_events :]
            self._persist_locked()
            return copy.deepcopy(event)

    def clear(self):
        with self._lock:
            self._events = []
            self._persist_locked()
            return {"updatedAt": self._updated_at, "events": []}


event_journal = EventJournal()
