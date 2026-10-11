import os
import stat
import tempfile
import unittest

from auth import hash_password, write_auth_env
from start_server import (
    ConfigurationError,
    build_environment,
    parse_env_file,
)


def auth_values(username="admin"):
    return {
        "APP_AUTH_USERNAME": username,
        "APP_AUTH_PASSWORD_HASH": hash_password(
            "correct horse battery staple", iterations=100_000
        ),
        "APP_AUTH_SECRET": "s" * 48,
    }


class AuthenticationEnvironmentWriterTest(unittest.TestCase):
    def test_password_reset_preserves_cell_location_and_other_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, ".auth.env")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(
                    "# legacy mixed configuration\n"
                    "CELL_GEOLOCATION_PROVIDER='unwired'\n"
                    "UNWIRED_LOCATION_API_TOKEN='must-be-preserved'\n"
                    "APP_AUTH_TOKEN_TTL_SECONDS=7200\n"
                    "APP_AUTH_USERNAME='old-admin'\n"
                    "APP_AUTH_PASSWORD_HASH='old-hash'\n"
                    "APP_AUTH_SECRET='old-secret'\n"
                )

            write_auth_env(path, auth_values("new-admin"))
            with open(path, "r", encoding="utf-8") as handle:
                text = handle.read()
            parsed = parse_env_file(path, required=True)

            self.assertIn("CELL_GEOLOCATION_PROVIDER='unwired'", text)
            self.assertIn(
                "UNWIRED_LOCATION_API_TOKEN='must-be-preserved'", text
            )
            self.assertEqual(parsed["UNWIRED_LOCATION_API_TOKEN"],
                             "must-be-preserved")
            self.assertEqual(parsed["APP_AUTH_TOKEN_TTL_SECONDS"], "7200")
            self.assertEqual(parsed["APP_AUTH_USERNAME"], "new-admin")
            self.assertNotIn("old-hash", text)
            self.assertNotIn("old-secret", text)
            self.assertEqual(text.count("APP_AUTH_USERNAME="), 1)
            if os.name == "posix":
                self.assertEqual(
                    stat.S_IMODE(os.stat(path).st_mode), 0o600
                )


class RuntimeConfigurationLoaderTest(unittest.TestCase):
    def write_file(self, directory, name, lines):
        path = os.path.join(directory, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(lines)
        return path

    def test_runtime_overrides_legacy_location_and_process_env_wins(self):
        with tempfile.TemporaryDirectory() as directory:
            values = auth_values()
            auth_path = self.write_file(
                directory,
                ".auth.env",
                "\n".join([
                    "APP_AUTH_USERNAME='{}'".format(
                        values["APP_AUTH_USERNAME"]
                    ),
                    "APP_AUTH_PASSWORD_HASH='{}'".format(
                        values["APP_AUTH_PASSWORD_HASH"]
                    ),
                    "APP_AUTH_SECRET='{}'".format(
                        values["APP_AUTH_SECRET"]
                    ),
                    "CELL_GEOLOCATION_PROVIDER=google",
                    "GOOGLE_GEOLOCATION_API_KEY=legacy-key",
                    "",
                ]),
            )
            runtime_path = self.write_file(
                directory,
                ".runtime.env",
                "CELL_GEOLOCATION_PROVIDER=unwired\n"
                "UNWIRED_LOCATION_API_TOKEN=runtime-token\n",
            )
            environment = build_environment(
                auth_path,
                runtime_path,
                process_env={
                    "UNWIRED_LOCATION_API_TOKEN": "nohup-env-token",
                    "APP_AUTH_USERNAME": "stale-shell-admin",
                },
            )
            self.assertEqual(
                environment["CELL_GEOLOCATION_PROVIDER"], "unwired"
            )
            self.assertEqual(
                environment["UNWIRED_LOCATION_API_TOKEN"],
                "nohup-env-token",
            )
            self.assertEqual(environment["APP_AUTH_USERNAME"], "admin")

    def test_runtime_file_cannot_replace_authentication_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            values = auth_values()
            auth_path = self.write_file(
                directory,
                ".auth.env",
                "\n".join(
                    "{}='{}'".format(key, value)
                    for key, value in values.items()
                ) + "\n",
            )
            runtime_path = self.write_file(
                directory,
                ".runtime.env",
                "APP_AUTH_SECRET='wrong-file'\n",
            )
            with self.assertRaises(ConfigurationError):
                build_environment(auth_path, runtime_path, process_env={})

    def test_example_location_token_placeholder_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            values = auth_values()
            auth_path = self.write_file(
                directory,
                ".auth.env",
                "\n".join(
                    "{}='{}'".format(key, value)
                    for key, value in values.items()
                ) + "\n",
            )
            runtime_path = self.write_file(
                directory,
                ".runtime.env",
                "CELL_GEOLOCATION_PROVIDER=unwired\n"
                "UNWIRED_LOCATION_API_TOKEN="
                "replace-with-your-server-side-token\n",
            )
            with self.assertRaises(ConfigurationError):
                build_environment(auth_path, runtime_path, process_env={})

    def test_missing_auth_file_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ConfigurationError):
                build_environment(
                    os.path.join(directory, ".auth.env"),
                    os.path.join(directory, ".runtime.env"),
                    process_env={},
                )


if __name__ == "__main__":
    unittest.main()
