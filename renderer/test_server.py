from io import BytesIO
import os
from threading import Thread
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from http.server import ThreadingHTTPServer

from PIL import Image
from server import Handler, SIZES, render_card


class RendererTests(unittest.TestCase):
    def test_each_model_has_native_card_dimensions(self):
        for model, dimensions in SIZES.items():
            with Image.open(BytesIO(render_card(model, [("Room", "21.5 C")]))) as image:
                self.assertEqual(image.size, dimensions)
                self.assertEqual(image.format, "JPEG")

    def test_http_requires_token_and_rejects_unknown_model(self):
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        server.renderer_token = "a-test-token-that-is-not-a-secret"
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{server.server_port}"
        try:
            with self.assertRaises(HTTPError) as error:
                urlopen(base + "/panel/ONX2432G028.jpg")
            self.assertEqual(error.exception.code, 401)
            headers = {"Authorization": "Bearer " + server.renderer_token}
            with self.assertRaises(HTTPError) as error:
                urlopen(Request(base + "/panel/UNKNOWN.jpg", headers=headers))
            self.assertEqual(error.exception.code, 404)
            with patch("server.ha_states", return_value=[("Room", "21 C")]):
                with urlopen(Request(base + "/panel/ONX3248G035.jpg", headers=headers)) as response:
                    self.assertEqual(response.headers["Content-Type"], "image/jpeg")
                    self.assertEqual(Image.open(BytesIO(response.read())).size, SIZES["ONX3248G035"])
            with patch("server.ha_states", side_effect=TimeoutError):
                with self.assertRaises(HTTPError) as error:
                    urlopen(Request(base + "/panel/ONX2432G028.jpg", headers=headers))
                self.assertEqual(error.exception.code, 503)
        finally:
            server.shutdown()
            thread.join()
            server.server_close()


if __name__ == "__main__":
    unittest.main()
