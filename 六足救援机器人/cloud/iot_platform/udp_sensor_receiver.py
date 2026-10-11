"""Validated unified telemetry UDP receiver and legacy BW21 control return path."""

import asyncio
import base64
import copy
import json
import math
import os
import queue
import socket
import threading
import time

from ws_gps import broadcast_gps_data
from ws_sensor import broadcast_sensor_data
from event_store import event_journal
from mjpeg_server import get_latest_jpeg


TELEMETRY_OFFLINE_SECONDS = float(os.getenv("TELEMETRY_OFFLINE_SECONDS", "5"))
TELEMETRY_DATA_STALE_SECONDS = float(os.getenv("TELEMETRY_DATA_STALE_SECONDS", "5"))
BW21_ADDRESS_TTL_SECONDS = 15.0
MAX_TELEMETRY_BYTES = 4096
CONTROL_REQUEST_ID_MAX = 64
PS2_BUTTONS = frozenset({"PAD_UP", "PAD_RIGHT", "PAD_DOWN", "PAD_LEFT"})
PS2_BUTTON_STATES = frozenset({"down", "up"})
UINT32_MASK = 0xFFFFFFFF
UINT32_HALF_RANGE = 0x80000000
MAX_CONTROL_SEQUENCE_DEVICES = 128
AP_STREAM_STATES = {
    "unknown", "unsupported", "disabled", "starting", "enabled", "stopping", "error",
}
MODEM_4G_STATES = {
    "unknown", "unsupported", "idle", "querying", "applying", "success", "error",
}
MODEM_4G_ACTIONS = {
    "query", "ping", "reselect", "set_bands", "set_cell_lock", "clear_cell_lock",
}
MODEM_LTE_BANDS = frozenset({1, 3, 5, 7, 8, 20, 34, 39, 40, 41})
MODEM_MAX_CELLS = 6
WIFI_STA_STATES = {
    "unknown", "unsupported", "idle", "applying", "working", "success", "error",
}
WIFI_STA_ACTIONS = {
    "query", "set_enabled", "scan", "connect", "select_uplink",
}
WIFI_SECURITY_MODES = frozenset({
    "open", "wpa", "wpa2", "wpa/wpa2", "wpa3", "wpa2/wpa3",
})
WIFI_MAX_NETWORKS = 10
VIDEO_FPS_OPTIONS = frozenset({5, 8, 15, 20, 30})
VIDEO_RESOLUTIONS = {
    "640x480": (640, 480),
    "1280x720": (1280, 720),
    "1920x1080": (1920, 1080),
}
ROBOT_CONTROL_BOOLEAN_FIELDS = frozenset({"terrain", "autonomous", "radar", "radar_ready"})
ROBOT_CONTROL_COMMANDS = frozenset({
    "body_height", "speed_percent", "terrain", "autonomous", "radar",
})


def _empty_sensor(online=False):
    return {
        "online": online,
        "device_id": None,
        "boot_id": None,
        "seq": None,
        "temperature": None,
        "humidity": None,
        "altitude": None,
        "pressure": None,
        "gas": None,
        "person_detected": None,
        "timestamp": None,
        "received_at": None,
        "age_ms": None,
    }


def _empty_gps(online=False):
    return {
        "online": online,
        "device_id": None,
        "boot_id": None,
        "seq": None,
        "lat": None,
        "lng": None,
        "satellites": None,
        "speed": None,
        "heading": None,
        "timestamp": None,
        "received_at": None,
        "age_ms": None,
    }


def _empty_wifi_sta_state():
    return {
        "device_id": None,
        "boot_id": None,
        "supported": None,
        "state": "unknown",
        "action": "query",
        "request_id": None,
        "error": None,
        "feature_enabled": False,
        "scanning": False,
        "connected": False,
        "ssid": "",
        "ip": "",
        "rssi": None,
        "wifi_uplink_selected": False,
        "active_uplink": "none",
        "networks": [],
        "received_at": None,
    }


def _empty_robot_control_state():
    return {
        "device_id": None,
        "boot_id": None,
        "supported": None,
        "body_height": None,
        "speed_percent": None,
        "terrain": None,
        "autonomous": None,
        "radar": None,
        "radar_ready": None,
        "received_at": None,
    }


_sensor_data = _empty_sensor()
_gps_data = _empty_gps()
_bw21_addr = None
_telemetry_last_seen = 0.0
_data_last_seen = 0.0
_esp_meta = {
    "device_id": None,
    "boot_id": None,
    "seq": None,
    "timestamp": None,
    "received_at": None,
}
_ap_stream_state = {
    "device_id": None,
    "boot_id": None,
    "supported": None,
    "state": "unknown",
    "request_id": None,
    "error": None,
    "received_at": None,
}
_modem_4g_state = {
    "device_id": None,
    "boot_id": None,
    "supported": None,
    "state": "unknown",
    "action": "query",
    "request_id": None,
    "error": None,
    "operator": None,
    "registration": None,
    "rssi": None,
    "rat": None,
    "band_config": "",
    "cell_lock": "",
    "cells": [],
    "received_at": None,
}
_video_fps_state = {
    "device_id": None,
    "boot_id": None,
    "supported": None,
    "fps": None,
    "resolution": "640x480",
    "width": 640,
    "height": 480,
    "request_id": None,
    "error": None,
    "received_at": None,
}
_wifi_sta_state = _empty_wifi_sta_state()
_robot_control_state = _empty_robot_control_state()
_last_sequence = {}
_control_sequences = {}
_sock = None
_state_lock = threading.Lock()
_alarm_state_lock = threading.Lock()
_alarm_state = {}
_alarm_event_queue = queue.Queue(maxsize=32)
_alarm_worker_started = False
_alarm_worker_lock = threading.Lock()


def _alarm_event_worker():
    while True:
        packet, event_type, received_ms = _alarm_event_queue.get()
        try:
            _persist_alarm_event(packet, event_type, received_ms)
        except (OSError, ValueError) as error:
            print(f"[EVENTS] failed to record {event_type}: {error}", flush=True)
        except Exception as error:
            print(f"[EVENTS] unexpected {event_type} record failure: {error}", flush=True)
        finally:
            _alarm_event_queue.task_done()


def _ensure_alarm_worker():
    global _alarm_worker_started
    if _alarm_worker_started:
        return
    with _alarm_worker_lock:
        if _alarm_worker_started:
            return
        threading.Thread(
            target=_alarm_event_worker,
            name="alarm-event-writer",
            daemon=True,
        ).start()
        _alarm_worker_started = True


def _enqueue_alarm_event(packet, event_type, received_ms):
    _ensure_alarm_worker()
    job = (copy.deepcopy(packet), event_type, received_ms)
    try:
        _alarm_event_queue.put_nowait(job)
        return True
    except queue.Full:
        # Alarm history is safety-relevant. If the bounded queue is saturated,
        # persist this rare overflow synchronously instead of silently losing it.
        print("[EVENTS] writer queue full; recording alarm synchronously", flush=True)
        _persist_alarm_event(*job)
        return True


def wait_for_alarm_events(timeout=5.0):
    """Wait until every queued alarm event has finished persisting.

    Queue.join() has no timeout on the Python version used by the deployed
    server. This mirrors its condition wait so shutdown can report a bounded
    failure instead of either dropping the last event or hanging forever.
    """
    if timeout is None:
        _alarm_event_queue.join()
        return True
    try:
        timeout = max(0.0, float(timeout))
    except (TypeError, ValueError):
        return False
    deadline = time.monotonic() + timeout
    with _alarm_event_queue.all_tasks_done:
        while _alarm_event_queue.unfinished_tasks:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return False
            _alarm_event_queue.all_tasks_done.wait(remaining)
    return True


def _alarm_boolean(value):
    if value is True or (isinstance(value, int) and not isinstance(value, bool) and value == 1):
        return True
    if value is False or (isinstance(value, int) and not isinstance(value, bool) and value == 0):
        return False
    return None


def _alarm_event_key(packet, event_type, received_ms):
    device_id = packet.get("device_id") or "unknown"
    boot_id = packet.get("boot_id") or "unknown"
    sequence = packet.get("seq")
    if isinstance(sequence, int) and not isinstance(sequence, bool):
        return f"telemetry:{device_id}:{boot_id}:{sequence}:{event_type}"
    return f"telemetry:{device_id}:{boot_id}:received:{received_ms}:{event_type}"


def _alarm_location(packet):
    gps = packet.get("gps")
    if not isinstance(gps, dict):
        return None
    lat = gps.get("lat")
    lng = gps.get("lng")
    if (not isinstance(lat, (int, float)) or isinstance(lat, bool)
            or not math.isfinite(lat) or not -90 <= lat <= 90
            or not isinstance(lng, (int, float)) or isinstance(lng, bool)
            or not math.isfinite(lng) or not -180 <= lng <= 180):
        return None
    return {"lat": float(lat), "lng": float(lng), "source": "gps"}


def _survivor_snapshot():
    frame_id, frame, metadata = get_latest_jpeg()
    if frame is None:
        return {
            "snapshotStatus": "error",
            "snapshotData": None,
            "frameId": None,
            "frameSequence": None,
        }
    encoded = base64.b64encode(frame).decode("ascii")
    return {
        "snapshotStatus": "ready",
        "snapshotData": f"data:image/jpeg;base64,{encoded}",
        "frameId": frame_id,
        "frameSequence": metadata.get("frame_seq"),
    }


def _persist_alarm_event(packet, event_type, received_ms):
    device_id = packet.get("device_id") or "unknown"
    event = {
        "id": f"alarm-{event_type}-{device_id}-{received_ms}",
        "type": event_type,
        "timestamp": received_ms,
        "deviceId": device_id,
        "bootId": packet.get("boot_id"),
        "sequence": packet.get("seq"),
        "dedupeKey": _alarm_event_key(packet, event_type, received_ms),
        "success": True,
        "operation": "识别到疑似幸存者" if event_type == "survivor" else "可燃气体超限",
        "message": "已保存识别时视频截图" if event_type == "survivor" else "检测到可燃气体报警信号",
        "location": _alarm_location(packet),
        "gas": copy.deepcopy(packet.get("sensor", {}).get("gas")) if event_type == "gas" else None,
        "snapshotStatus": "none",
        "snapshotData": None,
    }
    if event_type == "survivor":
        event.update(_survivor_snapshot())
    return event_journal.upsert(event)


def _record_alarm_edges(packet, received_at=None, persist_async=False):
    """Persist alarm rising edges even when no web, APP, or EXE is online."""
    if not packet or packet.get("status_only") or not isinstance(packet.get("sensor"), dict):
        return []
    sensor = packet["sensor"]
    person = _alarm_boolean(sensor.get("person_detected"))
    gas_value = sensor.get("gas")
    gas = _alarm_boolean(gas_value.get("alarm")) if isinstance(gas_value, dict) else _alarm_boolean(gas_value)
    if person is None and gas is None:
        return []

    received_at = time.time() if received_at is None else received_at
    received_ms = int(round(received_at * 1000))
    device_id = packet.get("device_id") or "unknown"
    boot_id = packet.get("boot_id") or "unknown"
    state_key = (device_id, boot_id)

    with _alarm_state_lock:
        state = _alarm_state.setdefault(state_key, {
            "person": False,
            "gas": False,
        })
        events = []
        if person is False:
            state["person"] = False
        if gas is False:
            state["gas"] = False
        if gas is True and not state["gas"]:
            state["gas"] = True
            events.append("gas")
        if person is True and not state["person"]:
            state["person"] = True
            events.append("survivor")
        if len(_alarm_state) > 128:
            current = _alarm_state[state_key]
            _alarm_state.clear()
            _alarm_state[state_key] = current

    saved = []
    for event_type in events:
        try:
            if persist_async:
                _enqueue_alarm_event(packet, event_type, received_ms)
                saved.append(event_type)
            else:
                saved.append(_persist_alarm_event(packet, event_type, received_ms))
        except (OSError, ValueError) as error:
            print(f"[EVENTS] failed to record {event_type}: {error}", flush=True)
    return saved


def _uint32_is_newer(current, previous):
    delta = (current - previous) & UINT32_MASK
    return 0 < delta < UINT32_HALF_RANGE


def _next_control_sequence(device_id):
    """Return a per-device uint32 sequence seeded from Unix time in milliseconds.

    The timestamp seed prevents a normally timed server restart from starting at
    zero behind an ESP that stayed online. Commands in the same millisecond (or
    after a backward clock adjustment) advance from the previous value. As with
    every int32/wrap-safe comparison, gaps of exactly or more than 2^31 are
    ambiguous and are deliberately treated as old by the ESP.
    """
    timestamp_seed = (time.time_ns() // 1_000_000) & UINT32_MASK
    with _state_lock:
        if (device_id not in _control_sequences
                and len(_control_sequences) >= MAX_CONTROL_SEQUENCE_DEVICES):
            del _control_sequences[next(iter(_control_sequences))]
        previous = _control_sequences.get(device_id)
        if previous is None or _uint32_is_newer(timestamp_seed, previous):
            sequence = timestamp_seed
        else:
            sequence = (previous + 1) & UINT32_MASK
        _control_sequences[device_id] = sequence
        return sequence


def _snapshot_locked(now):
    esp_fresh = (
        _telemetry_last_seen > 0
        and now - _telemetry_last_seen <= TELEMETRY_OFFLINE_SECONDS
    )
    if not esp_fresh:
        return _empty_sensor(), _empty_gps()

    data_fresh = (
        _data_last_seen > 0
        and now - _data_last_seen <= TELEMETRY_DATA_STALE_SECONDS
    )
    age_ms = max(0, int((now - _telemetry_last_seen) * 1000))
    if not data_fresh:
        # ESP/L610 is still online, but STM32 has not supplied a recent frame.
        sensor = {**_empty_sensor(True), **_esp_meta, "online": True, "age_ms": age_ms}
        gps = {**_empty_gps(True), **_esp_meta, "online": True, "age_ms": age_ms}
        return sensor, gps

    sensor = copy.deepcopy(_sensor_data)
    gps = copy.deepcopy(_gps_data)
    sensor.update({"online": True, "age_ms": age_ms})
    gps.update({"online": True, "age_ms": age_ms})
    return sensor, gps


def get_sensor_data():
    with _state_lock:
        sensor, _ = _snapshot_locked(time.monotonic())
        return sensor


def get_gps_data():
    with _state_lock:
        _, gps = _snapshot_locked(time.monotonic())
        return gps


def telemetry_online():
    with _state_lock:
        return (
            _telemetry_last_seen > 0
            and time.monotonic() - _telemetry_last_seen <= TELEMETRY_OFFLINE_SECONDS
        )


def get_ap_stream_state():
    with _state_lock:
        state = copy.deepcopy(_ap_stream_state)
        state["device_id"] = state.get("device_id") or _esp_meta.get("device_id")
        state["boot_id"] = state.get("boot_id") or _esp_meta.get("boot_id")
        state["online"] = (
            _telemetry_last_seen > 0
            and time.monotonic() - _telemetry_last_seen <= TELEMETRY_OFFLINE_SECONDS
        )
        return state


def get_modem_4g_state():
    with _state_lock:
        state = copy.deepcopy(_modem_4g_state)
        state["device_id"] = state.get("device_id") or _esp_meta.get("device_id")
        state["boot_id"] = state.get("boot_id") or _esp_meta.get("boot_id")
        state["online"] = (
            _telemetry_last_seen > 0
            and time.monotonic() - _telemetry_last_seen <= TELEMETRY_OFFLINE_SECONDS
        )
        return state


def get_video_fps_state():
    with _state_lock:
        state = copy.deepcopy(_video_fps_state)
        state["device_id"] = state.get("device_id") or _esp_meta.get("device_id")
        state["boot_id"] = state.get("boot_id") or _esp_meta.get("boot_id")
        state["online"] = (
            _telemetry_last_seen > 0
            and time.monotonic() - _telemetry_last_seen <= TELEMETRY_OFFLINE_SECONDS
        )
        return state


def get_wifi_sta_state():
    with _state_lock:
        state = copy.deepcopy(_wifi_sta_state)
        state["device_id"] = state.get("device_id") or _esp_meta.get("device_id")
        state["boot_id"] = state.get("boot_id") or _esp_meta.get("boot_id")
        state["online"] = (
            _telemetry_last_seen > 0
            and time.monotonic() - _telemetry_last_seen <= TELEMETRY_OFFLINE_SECONDS
        )
        return state


def get_robot_control_state():
    with _state_lock:
        state = copy.deepcopy(_robot_control_state)
        state["device_id"] = state.get("device_id") or _esp_meta.get("device_id")
        state["boot_id"] = state.get("boot_id") or _esp_meta.get("boot_id")
        state["online"] = (
            _telemetry_last_seen > 0
            and time.monotonic() - _telemetry_last_seen <= TELEMETRY_OFFLINE_SECONDS
        )
        return state


def get_control_state():
    with _state_lock:
        online = (
            _telemetry_last_seen > 0
            and time.monotonic() - _telemetry_last_seen <= TELEMETRY_OFFLINE_SECONDS
        )
        return {
            "device_id": _esp_meta.get("device_id"),
            "boot_id": _esp_meta.get("boot_id"),
            "online": online,
            "ap_stream": copy.deepcopy(_ap_stream_state),
            "modem_4g": copy.deepcopy(_modem_4g_state),
            "video_fps": copy.deepcopy(_video_fps_state),
            "wifi_sta": copy.deepcopy(_wifi_sta_state),
            "robot_control": copy.deepcopy(_robot_control_state),
        }


def _finite_number(value):
    return value is None or (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(value)
    )


def _validate_numbers(data, names):
    return all(_finite_number(data.get(name)) for name in names)


def _normalize_gas(value):
    """Normalize the STM32 digital gas flag while retaining legacy metadata."""
    if value is None:
        return True, None
    if isinstance(value, bool) or (
            isinstance(value, int) and not isinstance(value, bool) and value in (0, 1)):
        return True, {"alarm": bool(value)}
    if not isinstance(value, dict):
        return False, None

    normalized = copy.deepcopy(value)
    alarm = normalized.get("alarm")
    if alarm is not None:
        if not (isinstance(alarm, bool) or (
                isinstance(alarm, int) and not isinstance(alarm, bool)
                and alarm in (0, 1))):
            return False, None
        normalized["alarm"] = bool(alarm)
    return True, normalized


def _normalize_request_id(value):
    if value is None:
        return None
    if not isinstance(value, str):
        return None
    value = value.strip()
    if not value or len(value) > CONTROL_REQUEST_ID_MAX:
        return None
    return value


def _normalize_ap_stream(value, fallback_request_id=None):
    if value is None:
        return None
    if not isinstance(value, dict):
        return False

    supported = value.get("supported")
    if not isinstance(supported, bool):
        return False

    state = value.get("state")
    enabled = value.get("enabled")
    if isinstance(state, bool):
        state = "enabled" if state else "disabled"
    if state is None and isinstance(enabled, bool):
        state = "enabled" if enabled else "disabled"
    if state not in AP_STREAM_STATES:
        return False

    request_id_value = value.get("request_id", fallback_request_id)
    request_id = _normalize_request_id(request_id_value)
    if request_id_value is not None and request_id is None:
        return False

    error = value.get("error")
    if error is not None and (not isinstance(error, str) or len(error) > 256):
        return False

    return {
        "supported": supported,
        "state": state,
        "request_id": request_id,
        "error": error,
    }


def _bounded_int(value, minimum, maximum):
    return (
        isinstance(value, int) and not isinstance(value, bool)
        and minimum <= value <= maximum
    )


def _valid_wifi_credentials(ssid, password, security):
    if (not isinstance(ssid, str) or not ssid or "\x00" in ssid
            or not isinstance(password, str) or "\x00" in password
            or not isinstance(security, str)
            or security not in WIFI_SECURITY_MODES):
        return False
    try:
        ssid_length = len(ssid.encode("utf-8"))
        password_length = len(password.encode("utf-8"))
    except UnicodeEncodeError:
        return False
    if not 1 <= ssid_length <= 32 or password_length > 63:
        return False
    if security == "open":
        return password_length == 0
    return 8 <= password_length <= 63


def _normalize_modem_cell(value):
    if not isinstance(value, dict):
        return None
    if not isinstance(value.get("serving"), bool):
        return None
    if not _bounded_int(value.get("mcc"), 0, 999):
        return None
    if not _bounded_int(value.get("mnc"), 0, 999):
        return None
    tac = value.get("tac")
    cell_id = value.get("cell_id")
    if (not isinstance(tac, str) or not tac or len(tac) > 11
            or not isinstance(cell_id, str) or not cell_id or len(cell_id) > 15):
        return None
    if not _bounded_int(value.get("earfcn"), 0, 0xFFFFFFFF):
        return None
    if not _bounded_int(value.get("pci"), -1, 503):
        return None
    if not _bounded_int(value.get("band"), -1, 255):
        return None
    for field in ("rxlev", "rsrp", "rsrq"):
        if not _bounded_int(value.get(field), -1, 255):
            return None
    return {
        "serving": value["serving"],
        "mcc": value["mcc"],
        "mnc": value["mnc"],
        "tac": tac,
        "cell_id": cell_id,
        "earfcn": value["earfcn"],
        "pci": value["pci"],
        "band": value["band"],
        "rxlev": value["rxlev"],
        "rsrp": value["rsrp"],
        "rsrq": value["rsrq"],
    }


def _normalize_modem_4g(value, fallback_request_id=None):
    if value is None:
        return None
    if not isinstance(value, dict) or not isinstance(value.get("supported"), bool):
        return False
    state = value.get("state")
    action = value.get("action", "query")
    if state not in MODEM_4G_STATES or action not in MODEM_4G_ACTIONS:
        return False
    request_value = value.get("request_id", fallback_request_id)
    request_id = _normalize_request_id(request_value)
    if request_value is not None and request_id is None:
        return False
    error = value.get("error")
    operator = value.get("operator")
    rat = value.get("rat")
    band_config = value.get("band_config", "")
    cell_lock = value.get("cell_lock", "")
    if error is not None and (not isinstance(error, str) or len(error) > 256):
        return False
    if operator is not None and (not isinstance(operator, str) or len(operator) > 64):
        return False
    if rat is not None and (not isinstance(rat, str) or len(rat) > 16):
        return False
    if (not isinstance(band_config, str) or len(band_config) > 160
            or not isinstance(cell_lock, str) or len(cell_lock) > 128):
        return False
    registration = value.get("registration")
    rssi = value.get("rssi")
    if registration is not None and not _bounded_int(registration, 0, 255):
        return False
    if rssi is not None and not _bounded_int(rssi, 0, 99):
        return False
    cell_values = value.get("cells", [])
    if not isinstance(cell_values, list) or len(cell_values) > MODEM_MAX_CELLS:
        return False
    cells = []
    for item in cell_values:
        cell = _normalize_modem_cell(item)
        if cell is None:
            return False
        cells.append(cell)
    return {
        "supported": value["supported"],
        "state": state,
        "action": action,
        "request_id": request_id,
        "error": error,
        "operator": operator,
        "registration": registration,
        "rssi": rssi,
        "rat": rat,
        "band_config": band_config,
        "cell_lock": cell_lock,
        "cells": cells,
    }


def _normalize_video_fps(value, fallback_request_id=None):
    if value is None:
        return None
    if not isinstance(value, dict) or not isinstance(value.get("supported"), bool):
        return False
    fps = value.get("fps")
    if (not isinstance(fps, int) or isinstance(fps, bool)
            or fps not in VIDEO_FPS_OPTIONS):
        return False
    # All firmware before configurable resolution used VGA. Treating an
    # omitted field as VGA keeps those devices compatible without accepting
    # arbitrary dimensions from the network.
    resolution = value.get("resolution", "640x480")
    if resolution not in VIDEO_RESOLUTIONS:
        return False
    width, height = VIDEO_RESOLUTIONS[resolution]
    reported_width = value.get("width", width)
    reported_height = value.get("height", height)
    if reported_width != width or reported_height != height:
        return False
    request_value = value.get("request_id", fallback_request_id)
    request_id = _normalize_request_id(request_value)
    if request_value is not None and request_id is None:
        return False
    error = value.get("error")
    if error is not None and (not isinstance(error, str) or len(error) > 256):
        return False
    return {
        "supported": value["supported"],
        "fps": fps,
        "resolution": resolution,
        "width": width,
        "height": height,
        "request_id": request_id,
        "error": error,
    }


def _normalize_robot_control(value):
    """Whitelist robot motion-control telemetry without mixing it into sensors."""
    if value is None:
        return None
    if not isinstance(value, dict):
        return False
    body_height = value.get("body_height")
    speed_percent = value.get("speed_percent")
    if not _bounded_int(body_height, -30, 70):
        return False
    if not _bounded_int(speed_percent, 30, 100):
        return False
    normalized = {
        "supported": True,
        "body_height": body_height,
        "speed_percent": speed_percent,
    }
    for field in ROBOT_CONTROL_BOOLEAN_FIELDS:
        field_value = value.get(field)
        if (not isinstance(field_value, bool)
                and not _bounded_int(field_value, 0, 1)):
            return False
        normalized[field] = bool(field_value)
    return normalized


def _normalize_wifi_network(value):
    if not isinstance(value, dict):
        return None
    ssid = value.get("ssid")
    security = value.get("security")
    if not isinstance(ssid, str):
        return None
    try:
        ssid_length = len(ssid.encode("utf-8"))
    except UnicodeEncodeError:
        return None
    if (not ssid or "\x00" in ssid or ssid_length > 32
            or not isinstance(security, str)
            or security not in WIFI_SECURITY_MODES):
        return None
    if not _bounded_int(value.get("rssi"), -127, 0):
        return None
    if not _bounded_int(value.get("channel"), 1, 14):
        return None
    if (not isinstance(value.get("secured"), bool)
            or not isinstance(value.get("supported"), bool)):
        return None
    return {
        "ssid": ssid,
        "rssi": value["rssi"],
        "channel": value["channel"],
        "security": security,
        "secured": value["secured"],
        "supported": value["supported"],
    }


def _normalize_wifi_sta(value, fallback_request_id=None):
    if value is None:
        return None
    if not isinstance(value, dict) or not isinstance(value.get("supported"), bool):
        return False
    state = value.get("state")
    action = value.get("action", "query")
    if (not isinstance(state, str) or state not in WIFI_STA_STATES
            or not isinstance(action, str) or action not in WIFI_STA_ACTIONS):
        return False
    request_value = value.get("request_id", fallback_request_id)
    request_id = _normalize_request_id(request_value)
    if request_value is not None and request_id is None:
        return False
    error = value.get("error")
    if error is not None and (not isinstance(error, str) or len(error) > 256):
        return False
    boolean_fields = (
        "feature_enabled", "scanning", "connected", "wifi_uplink_selected",
    )
    if any(not isinstance(value.get(field), bool) for field in boolean_fields):
        return False
    ssid = value.get("ssid", "")
    ip = value.get("ip", "")
    if not isinstance(ssid, str):
        return False
    try:
        ssid_length = len(ssid.encode("utf-8"))
    except UnicodeEncodeError:
        return False
    if ("\x00" in ssid or ssid_length > 32
            or not isinstance(ip, str) or len(ip) > 45):
        return False
    rssi = value.get("rssi")
    if rssi is not None and not _bounded_int(rssi, -127, 0):
        return False
    active_uplink = value.get("active_uplink")
    if active_uplink not in ("none", "l610", "wifi"):
        return False
    network_values = value.get("networks", [])
    if not isinstance(network_values, list) or len(network_values) > WIFI_MAX_NETWORKS:
        return False
    networks = []
    for item in network_values:
        network = _normalize_wifi_network(item)
        if network is None:
            return False
        networks.append(network)
    # Deliberately construct a whitelist. A password accidentally included by
    # a device is never retained, returned by an API, or broadcast to clients.
    return {
        "supported": value["supported"],
        "state": state,
        "action": action,
        "request_id": request_id,
        "error": error,
        "feature_enabled": value["feature_enabled"],
        "scanning": value["scanning"],
        "connected": value["connected"],
        "ssid": ssid,
        "ip": ip,
        "rssi": rssi,
        "wifi_uplink_selected": value["wifi_uplink_selected"],
        "active_uplink": active_uplink,
        "networks": networks,
    }


def _normalize_packet(parsed):
    """Return one sensor+GPS packet; old flat BW21 JSON remains accepted."""
    if not isinstance(parsed, dict):
        return None

    if parsed.get("protocol") == "ESUT/1":
        device_id = str(parsed.get("device_id", "")).strip()
        boot_id = str(parsed.get("boot_id", "")).strip()
        if not device_id or len(device_id) > 64 or not boot_id or len(boot_id) > 64:
            return None

        ap_stream = _normalize_ap_stream(
            parsed.get("ap_stream"), parsed.get("request_id")
        )
        if ap_stream is False:
            return None
        modem_4g = _normalize_modem_4g(
            parsed.get("modem_4g"), parsed.get("request_id")
        )
        if modem_4g is False:
            return None
        video_fps = _normalize_video_fps(
            parsed.get("video_fps"), parsed.get("request_id")
        )
        if video_fps is False:
            return None
        wifi_sta = _normalize_wifi_sta(
            parsed.get("wifi_sta"), parsed.get("request_id")
        )
        if wifi_sta is False:
            return None
        robot_control = _normalize_robot_control(parsed.get("robot_control"))
        if robot_control is False:
            return None

        # Heartbeats identify the ESP/L610 path. They deliberately carry no
        # STM32 measurements and therefore never overwrite the last data.
        if parsed.get("kind") == "heartbeat":
            return {
                "device_id": device_id,
                "boot_id": boot_id,
                "seq": None,
                "timestamp": parsed.get("timestamp"),
                "sensor": None,
                "gps": None,
                "heartbeat": True,
                "status_only": True,
                "ap_stream": ap_stream,
                "modem_4g": modem_4g,
                "video_fps": video_fps,
                "wifi_sta": wifi_sta,
                "robot_control": robot_control,
                "legacy": False,
            }

        if parsed.get("kind") in ("control_state", "status"):
            if (ap_stream is None and modem_4g is None and video_fps is None
                    and wifi_sta is None and robot_control is None):
                return None
            return {
                "device_id": device_id,
                "boot_id": boot_id,
                "seq": None,
                "timestamp": parsed.get("timestamp"),
                "sensor": None,
                "gps": None,
                "heartbeat": False,
                "status_only": True,
                "ap_stream": ap_stream,
                "modem_4g": modem_4g,
                "video_fps": video_fps,
                "wifi_sta": wifi_sta,
                "robot_control": robot_control,
                "legacy": False,
            }

        sensor = parsed.get("sensor", {})
        gps = parsed.get("gps", {})
        seq = parsed.get("seq")
        if not isinstance(sensor, dict) or not isinstance(gps, dict):
            return None
        if not isinstance(seq, int) or isinstance(seq, bool) or not 0 <= seq <= 0xFFFFFFFF:
            return None
        if not _validate_numbers(sensor, ("temperature", "humidity", "altitude", "pressure")):
            return None
        if not _validate_numbers(gps, ("lat", "lng", "satellites", "speed", "heading")):
            return None
        gas_valid, gas = _normalize_gas(sensor.get("gas"))
        if not gas_valid:
            return None
        sensor = dict(sensor)
        sensor["gas"] = gas
        person = sensor.get("person_detected")
        if person is not None and person not in (0, 1, False, True):
            return None
        return {
            "device_id": device_id,
            "boot_id": boot_id,
            "seq": seq,
            "timestamp": parsed.get("timestamp"),
            "sensor": sensor,
            "gps": gps,
            "heartbeat": False,
            "status_only": False,
            "ap_stream": ap_stream,
            "modem_4g": modem_4g,
            "video_fps": video_fps,
            "wifi_sta": wifi_sta,
            "robot_control": robot_control,
            "legacy": False,
        }

    # Compatibility with the existing BW21 flat JSON packet on UDP 9093.
    numeric = (
        "temperature", "humidity", "altitude", "pressure",
        "lat", "lng", "satellites", "speed", "heading", "timestamp",
    )
    if not _validate_numbers(parsed, numeric):
        return None
    gas_valid, gas = _normalize_gas(parsed.get("gas"))
    if not gas_valid:
        return None
    if not any(parsed.get(name) is not None for name in
               ("temperature", "humidity", "altitude", "pressure", "lat", "lng")):
        return None
    sensor = dict(parsed)
    sensor["gas"] = gas
    return {
        "device_id": str(parsed.get("device_id") or "legacy-bw21"),
        "boot_id": str(parsed.get("boot_id") or "legacy"),
        "seq": parsed.get("seq"),
        "timestamp": parsed.get("timestamp"),
        "sensor": sensor,
        "gps": parsed,
        "heartbeat": False,
        "status_only": False,
        "ap_stream": None,
        "modem_4g": None,
        "video_fps": None,
        "wifi_sta": None,
        "robot_control": None,
        "legacy": True,
    }


def _is_new_sequence(packet):
    if (packet.get("heartbeat") or packet.get("status_only")
            or packet["legacy"] or packet["seq"] is None):
        return True
    key = (packet["device_id"], packet["boot_id"])
    previous = _last_sequence.get(key)
    current = packet["seq"]
    if previous is not None:
        delta = (current - previous) & 0xFFFFFFFF
        if delta == 0 or delta >= 0x80000000:
            return False
    _last_sequence[key] = current
    if len(_last_sequence) > 128:
        _last_sequence.clear()
        _last_sequence[key] = current
    return True


def _prepare_command_payload(cmd_dict, device_id=None, boot_id=None):
    command = cmd_dict.get("cmd")
    request_id = _normalize_request_id(cmd_dict.get("request_id"))
    if not device_id or not boot_id or request_id is None:
        return None

    payload = {
        "protocol": "ESCTL/1",
        "kind": "command",
        "cmd": command,
        "request_id": request_id,
        "device_id": device_id,
        "boot_id": boot_id,
    }
    if command == "ap_stream":
        enabled = cmd_dict.get("enabled")
        if not isinstance(enabled, bool):
            return None
        payload["params"] = {"enabled": enabled}
        return payload
    if command == "ps2_button":
        button = cmd_dict.get("button")
        state = cmd_dict.get("state")
        if (not isinstance(button, str) or button not in PS2_BUTTONS
                or not isinstance(state, str) or state not in PS2_BUTTON_STATES):
            return None
        payload["control_seq"] = _next_control_sequence(device_id)
        payload["params"] = {"button": button, "state": state}
        return payload
    if command == "robot_control":
        control = cmd_dict.get("control")
        value = cmd_dict.get("value")
        if control not in ROBOT_CONTROL_COMMANDS:
            return None
        if control == "body_height":
            valid = _bounded_int(value, -30, 70)
        elif control == "speed_percent":
            valid = _bounded_int(value, 30, 100)
        else:
            valid = isinstance(value, bool)
        if not valid:
            return None
        payload["control_seq"] = _next_control_sequence(device_id)
        payload["params"] = {
            "control": control,
            "value": int(value) if isinstance(value, bool) else value,
        }
        return payload
    if command == "modem_4g":
        action = cmd_dict.get("action")
        if action not in MODEM_4G_ACTIONS:
            return None
        params = {"action": action}
        if action == "set_bands":
            bands = cmd_dict.get("bands")
            if (not isinstance(bands, list) or not bands
                    or len(bands) > len(MODEM_LTE_BANDS)
                    or any(not isinstance(item, int) or isinstance(item, bool)
                           or item not in MODEM_LTE_BANDS for item in bands)
                    or len(set(bands)) != len(bands)):
                return None
            params["bands"] = bands
        elif action == "set_cell_lock":
            earfcn = cmd_dict.get("earfcn")
            pci = cmd_dict.get("pci")
            if not _bounded_int(earfcn, 0, 0xFFFFFFFF):
                return None
            if pci is not None and not _bounded_int(pci, 0, 503):
                return None
            params["earfcn"] = earfcn
            params["pci"] = pci
        payload["params"] = params
        return payload
    if command == "video_fps":
        fps = cmd_dict.get("fps")
        if (not isinstance(fps, int) or isinstance(fps, bool)
                or fps not in VIDEO_FPS_OPTIONS):
            return None
        payload["params"] = {"fps": fps}
        resolution = cmd_dict.get("resolution")
        if resolution is not None:
            if resolution not in VIDEO_RESOLUTIONS:
                return None
            payload["params"]["resolution"] = resolution
        return payload
    if command == "wifi_sta":
        action = cmd_dict.get("action")
        if not isinstance(action, str) or action not in WIFI_STA_ACTIONS:
            return None
        params = {"action": action}
        if action == "set_enabled":
            enabled = cmd_dict.get("enabled")
            if not isinstance(enabled, bool):
                return None
            params["enabled"] = enabled
        elif action == "connect":
            ssid = cmd_dict.get("ssid")
            password = cmd_dict.get("password")
            security = cmd_dict.get("security")
            if not _valid_wifi_credentials(ssid, password, security):
                return None
            params.update({
                "ssid": ssid,
                "password": password,
                "security": security,
            })
        elif action == "select_uplink":
            use_wifi = cmd_dict.get("use_wifi")
            if not isinstance(use_wifi, bool):
                return None
            params["use_wifi"] = use_wifi
        payload["params"] = params
        return payload
    return None


def send_command_to_bw21(cmd_dict):
    with _state_lock:
        sock = _sock
        addr = _bw21_addr
        device_id = _esp_meta.get("device_id")
        boot_id = _esp_meta.get("boot_id")
        freshness_limit = (
            TELEMETRY_OFFLINE_SECONDS
            if cmd_dict.get("cmd") in (
                "ap_stream", "ps2_button", "robot_control", "modem_4g", "video_fps", "wifi_sta",
            )
            else BW21_ADDRESS_TTL_SECONDS
        )
        fresh = (
            _telemetry_last_seen > 0
            and time.monotonic() - _telemetry_last_seen <= freshness_limit
        )
    if sock is None or addr is None or not fresh:
        print('[SENSOR-UDP] command rejected: no recent validated telemetry address', flush=True)
        return False
    command_payload = _prepare_command_payload(cmd_dict, device_id, boot_id)
    if command_payload is None:
        print('[SENSOR-UDP] command rejected: missing device identity or invalid fields', flush=True)
        return False
    try:
        payload = json.dumps(command_payload, separators=(',', ':')).encode('utf-8')
        sent = sock.sendto(payload, addr)
        print(
            f'[SENSOR-UDP] -> device {sent}B cmd={cmd_dict.get("cmd")} '
            f'request_id={cmd_dict.get("request_id")}',
            flush=True,
        )
        return sent == len(payload)
    except OSError as exc:
        print(f'[SENSOR-UDP] command send failed: {exc}', flush=True)
        return False


def _schedule_broadcast(loop, sensor, gps):
    loop.call_soon_threadsafe(
        lambda value=copy.deepcopy(sensor):
        asyncio.create_task(broadcast_sensor_data(value))
    )
    loop.call_soon_threadsafe(
        lambda value=copy.deepcopy(gps):
        asyncio.create_task(broadcast_gps_data(value))
    )


def _schedule_ap_stream_broadcast(loop, callback, state):
    if callback is None:
        return
    loop.call_soon_threadsafe(
        lambda value=copy.deepcopy(state):
        asyncio.create_task(callback(value))
    )


def _udp_sensor_thread(port, loop, stop_event, ap_stream_callback=None):
    global _sensor_data, _gps_data, _bw21_addr, _telemetry_last_seen, _data_last_seen
    global _esp_meta, _ap_stream_state, _modem_4g_state, _video_fps_state
    global _wifi_sta_state, _robot_control_state, _sock
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', port))
    sock.settimeout(0.25)
    with _state_lock:
        _sock = sock
    print(f'[SENSOR-UDP] listening :{port}, offline_timeout={TELEMETRY_OFFLINE_SECONDS:.1f}s', flush=True)
    valid_packets = 0
    invalid_packets = 0
    duplicate_packets = 0
    offline_announced = True

    try:
        while not stop_event.is_set():
            data = None
            addr = None
            try:
                data, addr = sock.recvfrom(MAX_TELEMETRY_BYTES + 1)
            except socket.timeout:
                pass
            except OSError:
                break

            if data is not None:
                if len(data) > MAX_TELEMETRY_BYTES:
                    packet = None
                else:
                    try:
                        packet = _normalize_packet(json.loads(data.decode('utf-8')))
                    except (UnicodeDecodeError, json.JSONDecodeError):
                        packet = None
                if packet is None:
                    invalid_packets += 1
                    continue

                control_snapshot = None
                with _state_lock:
                    if not _is_new_sequence(packet):
                        duplicate_packets += 1
                        continue

                    now_wall = time.time()
                    meta = {
                        "online": True,
                        "device_id": packet["device_id"],
                        "boot_id": packet["boot_id"],
                        "seq": packet["seq"],
                        "timestamp": packet["timestamp"],
                        "received_at": now_wall,
                        "age_ms": 0,
                    }
                    _esp_meta = {
                        "device_id": packet["device_id"],
                        "boot_id": packet["boot_id"],
                        "seq": packet["seq"],
                        "timestamp": packet["timestamp"],
                        "received_at": now_wall,
                    }
                    if not packet.get("status_only"):
                        sensor = packet["sensor"]
                        gps = packet["gps"]
                        _sensor_data = {
                            **meta,
                            "temperature": sensor.get("temperature"),
                            "humidity": sensor.get("humidity"),
                            "altitude": sensor.get("altitude"),
                            "pressure": sensor.get("pressure"),
                            "gas": copy.deepcopy(sensor.get("gas")),
                            "person_detected": sensor.get("person_detected"),
                        }
                        _gps_data = {
                            **meta,
                            "lat": gps.get("lat"),
                            "lng": gps.get("lng"),
                            "satellites": gps.get("satellites"),
                            "speed": gps.get("speed"),
                            "heading": gps.get("heading"),
                        }
                        _data_last_seen = time.monotonic()
                    if packet.get("ap_stream") is not None:
                        _ap_stream_state = {
                            "device_id": packet["device_id"],
                            "boot_id": packet["boot_id"],
                            **packet["ap_stream"],
                            "received_at": now_wall,
                        }
                    if packet.get("modem_4g") is not None:
                        _modem_4g_state = {
                            "device_id": packet["device_id"],
                            "boot_id": packet["boot_id"],
                            **packet["modem_4g"],
                            "received_at": now_wall,
                        }
                    if packet.get("video_fps") is not None:
                        _video_fps_state = {
                            "device_id": packet["device_id"],
                            "boot_id": packet["boot_id"],
                            **packet["video_fps"],
                            "received_at": now_wall,
                        }
                    if packet.get("wifi_sta") is not None:
                        _wifi_sta_state = {
                            "device_id": packet["device_id"],
                            "boot_id": packet["boot_id"],
                            **packet["wifi_sta"],
                            "received_at": now_wall,
                        }
                    if packet.get("robot_control") is not None:
                        _robot_control_state = {
                            "device_id": packet["device_id"],
                            "boot_id": packet["boot_id"],
                            **packet["robot_control"],
                            "received_at": now_wall,
                        }
                    if (packet.get("ap_stream") is not None
                            or packet.get("modem_4g") is not None
                            or packet.get("video_fps") is not None
                            or packet.get("wifi_sta") is not None
                            or packet.get("robot_control") is not None):
                        control_snapshot = {
                            "device_id": packet["device_id"],
                            "boot_id": packet["boot_id"],
                            "online": True,
                            "ap_stream": copy.deepcopy(_ap_stream_state),
                            "modem_4g": copy.deepcopy(_modem_4g_state),
                            "video_fps": copy.deepcopy(_video_fps_state),
                            "wifi_sta": copy.deepcopy(_wifi_sta_state),
                            "robot_control": copy.deepcopy(_robot_control_state),
                        }
                    _bw21_addr = addr
                    _telemetry_last_seen = time.monotonic()
                    sensor_snapshot, gps_snapshot = _snapshot_locked(_telemetry_last_seen)

                _record_alarm_edges(packet, now_wall, persist_async=True)
                _schedule_broadcast(loop, sensor_snapshot, gps_snapshot)
                if control_snapshot is not None:
                    _schedule_ap_stream_broadcast(
                        loop, ap_stream_callback, control_snapshot
                    )
                offline_announced = False
                valid_packets += 1
                if valid_packets <= 3 or valid_packets % 50 == 0:
                    print(
                        f'[SENSOR-UDP] valid {"heartbeat" if packet.get("heartbeat") else "data"} packet#{valid_packets} '
                        f'device={packet["device_id"]} seq={packet["seq"]} '
                        f'from {addr[0]}:{addr[1]}',
                        flush=True,
                    )

            with _state_lock:
                stale = (
                    _telemetry_last_seen > 0
                    and time.monotonic() - _telemetry_last_seen > TELEMETRY_OFFLINE_SECONDS
                )
            if stale and not offline_announced:
                offline_announced = True
                _schedule_broadcast(loop, _empty_sensor(), _empty_gps())
                with _state_lock:
                    control_snapshot = {
                        "device_id": _esp_meta.get("device_id"),
                        "boot_id": _esp_meta.get("boot_id"),
                        "online": False,
                        "ap_stream": copy.deepcopy(_ap_stream_state),
                        "modem_4g": copy.deepcopy(_modem_4g_state),
                        "video_fps": copy.deepcopy(_video_fps_state),
                        "wifi_sta": copy.deepcopy(_wifi_sta_state),
                        "robot_control": copy.deepcopy(_robot_control_state),
                    }
                _schedule_ap_stream_broadcast(
                    loop, ap_stream_callback, control_snapshot
                )
                print('[SENSOR-UDP] telemetry timeout; device offline and values cleared', flush=True)
    finally:
        with _state_lock:
            _sock = None
            _bw21_addr = None
            _telemetry_last_seen = 0.0
            _data_last_seen = 0.0
            _esp_meta = {
                "device_id": None, "boot_id": None, "seq": None,
                "timestamp": None, "received_at": None,
            }
            _ap_stream_state = {
                "device_id": None, "boot_id": None, "supported": None,
                "state": "unknown", "request_id": None, "error": None,
                "received_at": None,
            }
            _modem_4g_state = {
                "device_id": None, "boot_id": None, "supported": None,
                "state": "unknown", "action": "query", "request_id": None,
                "error": None, "operator": None, "registration": None,
                "rssi": None, "rat": None, "band_config": "",
                "cell_lock": "", "cells": [], "received_at": None,
            }
            _video_fps_state = {
                "device_id": None, "boot_id": None, "supported": None,
                "fps": None, "resolution": "640x480", "width": 640,
                "height": 480, "request_id": None, "error": None,
                "received_at": None,
            }
            _wifi_sta_state = _empty_wifi_sta_state()
            _robot_control_state = _empty_robot_control_state()
        sock.close()
        _schedule_broadcast(loop, _empty_sensor(), _empty_gps())
        _schedule_ap_stream_broadcast(
            loop, ap_stream_callback, {
                "device_id": None,
                "boot_id": None,
                "online": False,
                "ap_stream": copy.deepcopy(_ap_stream_state),
                "modem_4g": copy.deepcopy(_modem_4g_state),
                "video_fps": copy.deepcopy(_video_fps_state),
                "wifi_sta": copy.deepcopy(_wifi_sta_state),
                "robot_control": copy.deepcopy(_robot_control_state),
            }
        )
        print(
            f'[SENSOR-UDP] stopped, valid={valid_packets}, invalid={invalid_packets}, '
            f'duplicate={duplicate_packets}',
            flush=True,
        )


async def start_udp_sensor_receiver(port=9093, ap_stream_callback=None):
    stop_event = threading.Event()
    thread = threading.Thread(
        target=_udp_sensor_thread,
        args=(port, asyncio.get_running_loop(), stop_event, ap_stream_callback),
        daemon=True,
    )
    thread.start()
    try:
        while thread.is_alive():
            await asyncio.sleep(0.5)
    except asyncio.CancelledError:
        stop_event.set()
        raise
    finally:
        stop_event.set()
        thread.join(timeout=1.5)
