"""Optional HTTP JPEG cards for Room Companion; HA credentials stay on this host."""
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
import json
import os
import secrets
from urllib.error import URLError
from urllib.parse import quote, urlsplit
from urllib.request import Request, urlopen

from PIL import Image, ImageDraw, ImageFont

SIZES = {"ONX2432G028": (208, 144), "ONX3248G035": (288, 288)}


def ha_states():
    base = os.environ.get("HA_URL", "").rstrip("/")
    token = os.environ.get("HA_TOKEN", "")
    entities = [v.strip() for v in os.environ.get("HA_ENTITIES", "").split(",") if v.strip()][:6]
    if not base or not token or not entities:
        return [("Room Companion", "Configure HA_URL, HA_TOKEN and HA_ENTITIES")]
    rows = []
    for entity in entities:
        req = Request(base + "/api/states/" + quote(entity, safe=""), headers={"Authorization": "Bearer " + token})
        with urlopen(req, timeout=3) as response:
            state = json.load(response)
        attrs = state.get("attributes", {})
        rows.append((str(attrs.get("friendly_name", entity))[:80],
                     (str(state.get("state", "unknown")) + " " + str(attrs.get("unit_of_measurement", ""))).strip()[:120]))
    return rows


def render_card(model, rows):
    width, height = SIZES[model]
    image = Image.new("RGB", (width, height), "#182633")
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default(size=14)
    small = ImageFont.load_default(size=11)
    draw.text((12, 10), "TODAY", font=small, fill="#91DDD1")
    draw.text((12, 28), datetime.now().astimezone().strftime("%a %d %b  %H:%M"), font=font, fill="#EDF2F4")
    y = 56
    for name, value in rows:
        if y + 36 > height - 8:
            break
        # Deliberately bound layout to each card; the small screen shows fewer rows.
        draw.text((12, y), name[:max(12, (width - 24) // 8)], font=small, fill="#A8B8C4")
        draw.text((12, y + 15), value[:max(12, (width - 24) // 8)], font=font, fill="#EDF2F4")
        y += 44
    output = BytesIO()
    image.save(output, "JPEG", quality=85)
    return output.getvalue()


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        expected = "Bearer " + self.server.renderer_token
        supplied = self.headers.get("Authorization", "")
        if not secrets.compare_digest(supplied.encode(), expected.encode()):
            self.send_error(401)
            return
        path = urlsplit(self.path).path
        model = path.removeprefix("/panel/").removesuffix(".jpg")
        if path != f"/panel/{model}.jpg" or model not in SIZES:
            self.send_error(404)
            return
        try:
            payload = render_card(model, ha_states())
        except (URLError, TimeoutError, ValueError, KeyError, OSError):
            self.send_error(503, "Home Assistant data unavailable")
            return
        self.send_response(200)
        self.send_header("Content-Type", "image/jpeg")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        # Log only status codes; no URLs, tokens, entity names or rendered contents.
        if len(args) > 1:
            print("renderer HTTP", args[1], flush=True)


def main():
    token = os.environ.get("RENDERER_TOKEN", "")
    if len(token) < 24:
        raise SystemExit("Set RENDERER_TOKEN to at least 24 characters")
    server = ThreadingHTTPServer((os.environ.get("BIND_ADDRESS", "127.0.0.1"), int(os.environ.get("PORT", "8099"))), Handler)
    server.renderer_token = token
    print("Room Companion renderer ready", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
