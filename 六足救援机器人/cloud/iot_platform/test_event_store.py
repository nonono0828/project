import tempfile
import unittest
from pathlib import Path

from event_store import EventJournal, MAX_SNAPSHOT_LENGTH


class EventJournalTest(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.path = Path(self.tempdir.name) / "events.json"
        self.journal = EventJournal(self.path, max_events=3)

    def tearDown(self):
        self.tempdir.cleanup()

    def event(self, event_id, timestamp=1, dedupe_key=None):
        return {
            "id": event_id,
            "type": "location",
            "timestamp": timestamp,
            "dedupeKey": dedupe_key,
            "message": "位置来源：GPS",
            "location": {"lat": 23.1, "lng": 113.2, "source": "gps"},
        }

    def test_events_survive_process_reload(self):
        self.journal.upsert(self.event("first", 10))
        restored = EventJournal(self.path, max_events=3).snapshot()
        self.assertEqual(restored["events"][0]["id"], "first")
        self.assertEqual(restored["events"][0]["location"]["source"], "gps")

    def test_dedupe_key_keeps_one_record(self):
        self.journal.upsert(self.event("first", 10, "location:robot:1"))
        saved = self.journal.upsert(self.event("second", 20, "location:robot:1"))
        snapshot = self.journal.snapshot()
        self.assertEqual(saved["id"], "first")
        self.assertEqual(len(snapshot["events"]), 1)
        self.assertEqual(snapshot["events"][0]["timestamp"], 20)

    def test_frontend_and_backend_alarm_key_keeps_one_record(self):
        dedupe_key = "telemetry:spider-001:boot-a:42:survivor"
        backend = self.event("backend-survivor", 100, dedupe_key)
        backend.update({"type": "survivor", "deviceId": "spider-001"})
        frontend = self.event("frontend-survivor", 101, dedupe_key)
        frontend.update({"type": "survivor", "deviceId": "spider-001"})

        self.journal.upsert(backend)
        saved = self.journal.upsert(frontend)
        snapshot = self.journal.snapshot()

        self.assertEqual(saved["id"], "backend-survivor")
        self.assertEqual(len(snapshot["events"]), 1)
        self.assertEqual(snapshot["events"][0]["timestamp"], 101)

    def test_distinct_survivor_edges_are_not_time_merged(self):
        first = self.event("survivor-1", 100_000)
        first.update({"type": "survivor", "deviceId": "robot-a", "dedupeKey": "frame:1"})
        duplicate = self.event("survivor-2", 109_999)
        duplicate.update({"type": "survivor", "deviceId": "robot-a", "dedupeKey": "frame:2"})
        boundary = self.event("survivor-3", 110_000)
        boundary.update({"type": "survivor", "deviceId": "robot-a", "dedupeKey": "frame:3"})

        self.journal.upsert(first)
        saved_duplicate = self.journal.upsert(duplicate)
        self.journal.upsert(boundary)
        snapshot = self.journal.snapshot()

        self.assertEqual(saved_duplicate["id"], "survivor-2")
        self.assertEqual(len(snapshot["events"]), 3)
        self.assertEqual(snapshot["events"][2]["timestamp"], 100_000)

    def test_snapshot_and_event_limit_are_enforced(self):
        for index in range(4):
            event = self.event(f"event-{index}", index)
            event["snapshotData"] = "data:image/jpeg;base64,AA=="
            self.journal.upsert(event)
        snapshot = self.journal.snapshot()
        self.assertEqual(len(snapshot["events"]), 3)
        self.assertEqual(snapshot["events"][0]["snapshotStatus"], "ready")

    def test_maximum_supported_jpeg_data_url_is_persisted(self):
        event = self.event("survivor-large", 100)
        event.update({
            "type": "survivor",
            "snapshotStatus": "ready",
            "snapshotData": "data:image/jpeg;base64," + "A" * 699_052,
            "frameId": 17,
            "frameSequence": 23,
        })
        saved = self.journal.upsert(event)
        self.assertLess(len(event["snapshotData"]), MAX_SNAPSHOT_LENGTH)
        self.assertEqual(saved["snapshotStatus"], "ready")
        self.assertEqual(saved["frameId"], 17)
        self.assertEqual(saved["frameSequence"], 23)

    def test_robot_control_event_is_supported(self):
        event = self.event("terrain-on", 200)
        event.update({
            "type": "robot_control",
            "success": True,
            "operation": "越野模式已开启",
            "message": "设备已确认状态变化",
            "location": None,
        })
        saved = self.journal.upsert(event)
        self.assertEqual(saved["type"], "robot_control")
        self.assertEqual(saved["operation"], "越野模式已开启")

    def test_clear_is_persistent(self):
        self.journal.upsert(self.event("first"))
        self.journal.clear()
        self.assertEqual(EventJournal(self.path).snapshot()["events"], [])


if __name__ == "__main__":
    unittest.main()
