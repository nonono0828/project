import unittest

from auth import (
    REALTIME_TICKET_SCOPE,
    REALTIME_TICKET_TTL_SECONDS,
    AuthManager,
    LoginRateLimiter,
    hash_password,
    verify_password,
)


class PasswordHashTest(unittest.TestCase):
    def test_password_hash_verification(self):
        encoded = hash_password(
            "correct horse battery staple",
            iterations=100_000,
            salt=b"0123456789abcdef",
        )
        self.assertTrue(verify_password("correct horse battery staple", encoded))
        self.assertFalse(verify_password("wrong", encoded))
        self.assertNotIn("correct horse battery staple", encoded)


class AccessTokenTest(unittest.TestCase):
    def manager(self):
        return AuthManager(
            "admin",
            hash_password(
                "correct horse battery staple",
                iterations=100_000,
                salt=b"0123456789abcdef",
            ),
            "test-secret-that-is-at-least-thirty-two-bytes",
            ttl_seconds=3600,
        )

    def test_authenticate_and_verify(self):
        manager = self.manager()
        result = manager.authenticate(
            "admin", "correct horse battery staple", now=1000
        )
        self.assertEqual(result["username"], "admin")
        claims = manager.verify_token(result["access_token"], now=1200)
        self.assertEqual(claims["sub"], "admin")
        self.assertEqual(claims["exp"], 4600)

    def test_wrong_password_tamper_and_expiry_are_rejected(self):
        manager = self.manager()
        self.assertIsNone(manager.authenticate("admin", "wrong", now=1000))
        result = manager.authenticate(
            "admin", "correct horse battery staple", now=1000
        )
        token = result["access_token"]
        self.assertIsNone(manager.verify_token(token + "x", now=1200))
        self.assertIsNone(manager.verify_token(token, now=4600))

    def test_realtime_ticket_is_short_lived_and_cannot_be_used_as_access_token(self):
        manager = self.manager()
        result = manager.issue_realtime_ticket(now=1000)
        self.assertEqual(result["scope"], REALTIME_TICKET_SCOPE)
        self.assertEqual(
            result["expires_at"] - result["issued_at"],
            REALTIME_TICKET_TTL_SECONDS,
        )
        claims = manager.verify_realtime_ticket(result["ticket"], now=1020)
        self.assertEqual(claims["scope"], REALTIME_TICKET_SCOPE)
        self.assertIsNone(manager.verify_token(result["ticket"], now=1020))
        self.assertIsNone(
            manager.verify_realtime_ticket(result["ticket"], now=1060)
        )

    def test_access_token_and_wrong_scope_are_rejected_as_realtime_ticket(self):
        manager = self.manager()
        access = manager.issue_token(now=1000)["access_token"]
        self.assertIsNone(manager.verify_realtime_ticket(access, now=1020))
        wrong_scope_claims = {
            "sub": "admin",
            "iat": 1000,
            "exp": 1060,
            "scope": "api",
            "nonce": "test",
        }
        wrong_scope = manager._sign_claims("rt1", wrong_scope_claims)
        self.assertIsNone(manager.verify_realtime_ticket(wrong_scope, now=1020))


class LoginRateLimiterTest(unittest.TestCase):
    def test_failure_window_and_success_clear(self):
        limiter = LoginRateLimiter(max_attempts=2, window_seconds=60)
        limiter.record_failure("client", now=10)
        self.assertFalse(limiter.blocked("client", now=20))
        limiter.record_failure("client", now=20)
        self.assertTrue(limiter.blocked("client", now=30))
        self.assertFalse(limiter.blocked("client", now=90))
        limiter.record_failure("client", now=100)
        limiter.clear("client")
        self.assertFalse(limiter.blocked("client", now=101))


if __name__ == "__main__":
    unittest.main()
