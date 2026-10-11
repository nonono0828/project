import os
import unittest
from unittest.mock import patch

from aiohttp import web

from auth import AuthManager, hash_password
from server import authenticated_control_surface


class _Request:
    def __init__(self, path, method='GET', headers=None, query=None):
        self.path = path
        self.method = method
        self.headers = headers or {}
        self.query = query or {}
        self.state = {}

    def __setitem__(self, key, value):
        self.state[key] = value


async def _ok_handler(_request):
    return web.Response(status=204)


class AuthenticationMiddlewareTest(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.env = {
            'APP_AUTH_USERNAME': 'admin',
            'APP_AUTH_PASSWORD_HASH': hash_password(
                'correct horse battery staple', iterations=100_000
            ),
            'APP_AUTH_SECRET': 's' * 48,
            'APP_AUTH_TOKEN_TTL_SECONDS': '3600',
        }

    async def test_public_login_and_health_do_not_require_token(self):
        with patch.dict(os.environ, {}, clear=True):
            for path in ('/api/auth/login', '/api/health', '/api/ready'):
                response = await authenticated_control_surface(
                    _Request(path), _ok_handler
                )
                self.assertEqual(response.status, 204)

    async def test_business_http_and_websocket_reject_anonymous_request(self):
        with patch.dict(os.environ, self.env, clear=True):
            for path in (
                '/api/device/status',
                '/api/auth/realtime-ticket',
                '/ws/control',
                '/live/mjpeg',
            ):
                response = await authenticated_control_surface(
                    _Request(path), _ok_handler
                )
                self.assertEqual(response.status, 401)

    async def test_bearer_is_api_only_and_realtime_ticket_is_handshake_only(self):
        with patch.dict(os.environ, self.env, clear=True):
            manager = AuthManager.from_env()
            token = manager.authenticate(
                'admin', 'correct horse battery staple'
            )['access_token']
            ticket = manager.issue_realtime_ticket()['ticket']

            header_request = _Request(
                '/api/device/status',
                headers={'Authorization': f'Bearer {token}'},
            )
            header_response = await authenticated_control_surface(
                header_request, _ok_handler
            )
            self.assertEqual(header_response.status, 204)
            self.assertEqual(header_request.state['auth_claims']['sub'], 'admin')

            api_query_response = await authenticated_control_surface(
                _Request(
                    '/api/device/status', query={'access_token': token}
                ),
                _ok_handler,
            )
            self.assertEqual(api_query_response.status, 401)

            ticket_as_bearer_response = await authenticated_control_surface(
                _Request(
                    '/api/device/status',
                    headers={'Authorization': f'Bearer {ticket}'},
                ),
                _ok_handler,
            )
            self.assertEqual(ticket_as_bearer_response.status, 401)

            ticket_request = _Request(
                '/ws/control', query={'ticket': ticket}
            )
            ticket_response = await authenticated_control_surface(
                ticket_request, _ok_handler
            )
            self.assertEqual(ticket_response.status, 204)
            self.assertEqual(
                ticket_request.state['auth_claims']['scope'], 'realtime'
            )

            access_query_response = await authenticated_control_surface(
                _Request('/ws/control', query={'access_token': token}),
                _ok_handler,
            )
            self.assertEqual(access_query_response.status, 401)

            access_header_response = await authenticated_control_surface(
                _Request(
                    '/live/mjpeg',
                    headers={'Authorization': f'Bearer {token}'},
                ),
                _ok_handler,
            )
            self.assertEqual(access_header_response.status, 401)

    async def test_unconfigured_server_fails_closed(self):
        with patch.dict(os.environ, {}, clear=True):
            response = await authenticated_control_surface(
                _Request('/api/device/status'), _ok_handler
            )
            self.assertEqual(response.status, 503)


if __name__ == '__main__':
    unittest.main()
