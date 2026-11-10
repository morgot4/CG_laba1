from collections.abc import Callable
from math import floor, hypot, pi, sin
from pathlib import Path
import struct
import zlib


OUTPUT = Path(__file__).resolve().parents[1] / 'textures'
WIDTH = 512
HEIGHT = 256
GRADIENTS = ((1.0, 0.0), (-1.0, 0.0), (0.0, 1.0), (0.0, -1.0),
             (0.7071, 0.7071), (-0.7071, 0.7071), (0.7071, -0.7071), (-0.7071, -0.7071))


def chunk(*, kind: bytes, data: bytes) -> bytes:
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def write_png(*, name: str, width: int, height: int, pixels: bytes) -> None:
    rows = b''.join(b'\x00' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    header = struct.pack('>2I5B', width, height, 8, 2, 0, 0, 0)
    data = b'\x89PNG\r\n\x1a\n' + chunk(kind=b'IHDR', data=header)
    data += chunk(kind=b'IDAT', data=zlib.compress(rows, 9)) + chunk(kind=b'IEND', data=b'')
    (OUTPUT / name).write_bytes(data)


def fade(value: float) -> float:
    return value ** 3 * (value * (value * 6 - 15) + 10)


def gradient_dot(*, x: int, y: int, dx: float, dy: float, period: int) -> float:
    index = ((x % period) * 1836311903 ^ (y % period) * 2971215073) & 7
    gradient = GRADIENTS[index]
    return gradient[0] * dx + gradient[1] * dy


def perlin(*, u: float, v: float, period: int) -> float:
    x, y = u * period, v * period
    ix, iy = floor(x), floor(y)
    dx, dy = x - ix, y - iy
    a = gradient_dot(x=ix, y=iy, dx=dx, dy=dy, period=period)
    b = gradient_dot(x=ix + 1, y=iy, dx=dx - 1, dy=dy, period=period)
    c = gradient_dot(x=ix, y=iy + 1, dx=dx, dy=dy - 1, period=period)
    d = gradient_dot(x=ix + 1, y=iy + 1, dx=dx - 1, dy=dy - 1, period=period)
    top = a + fade(dx) * (b - a)
    bottom = c + fade(dx) * (d - c)
    return top + fade(dy) * (bottom - top)


def fractal_noise(*, u: float, v: float) -> float:
    return sum(perlin(u=u, v=v, period=4 * 2 ** octave) / 2 ** octave for octave in range(4))


def ceramic(*, u: float, v: float) -> dict[str, list[float]]:
    tile_u, tile_v = (u * 16) % 1, (v * 8) % 1
    grout = min(tile_u, 1 - tile_u, tile_v, 1 - tile_v) < 0.035
    checker = (floor(u * 16) + floor(v * 8)) % 2
    tone = 0.92 + 0.04 * sin(2 * pi * u * 64) * sin(2 * pi * v * 32)
    base = [0.08, 0.38, 0.47] if checker else [0.88, 0.84, 0.70]
    color = [0.055] * 3 if grout else [channel * tone for channel in base]
    specular = 0.0 if grout or checker else 0.85
    return {'albedo': color, 'specular': [specular] * 3}


def copper(*, u: float, v: float) -> dict[str, list[float]]:
    seam = min((u * 12) % 1, (v * 6) % 1) < 0.045
    rivet = hypot((u * 12) % 1 - 0.12, (v * 6) % 1 - 0.12) < 0.045
    brushing = 0.86 + 0.08 * sin(2 * pi * v * 120) + 0.04 * sin(2 * pi * u * 7)
    base = [0.72, 0.39, 0.18] if not seam else [0.12, 0.22, 0.19]
    color = [channel * brushing for channel in base] if not rivet else [0.9, 0.73, 0.44]
    return {'albedo': color, 'specular': [0.03 if seam else 0.95] * 3}


def lava(*, u: float, v: float) -> dict[str, list[float]]:
    noise = fractal_noise(u=u, v=v)
    vein = max(0.0, 1.0 - abs(noise) / 0.10) ** 2
    rock = 0.06 + 0.12 * min(abs(noise) * 3, 1)
    albedo = [rock + 0.6 * vein, rock * 0.8 + 0.13 * vein, rock * 0.7]
    emissive = [vein, vein ** 2 * 0.48, vein ** 4 * 0.05]
    return {'albedo': albedo, 'specular': [0.04 + vein * 0.5] * 3, 'emissive': emissive}


def uv_grid(*, u: float, v: float) -> dict[str, list[float]]:
    line = min((u * 16) % 1, (v * 8) % 1) < 0.055
    color = [0.035, 0.035, 0.035] if line else [0.2 + 0.75 * u, 0.2 + 0.75 * v, 0.5]
    if abs(v - 0.5) < 0.006:
        color = [1.0, 1.0, 1.0]
    return {'grid': color}


def generate(*, prefix: str, sample: Callable[..., dict[str, list[float]]]) -> None:
    layers: dict[str, bytearray] = {}
    for index in range(WIDTH * HEIGHT):
        values = sample(u=(index % WIDTH + 0.5) / WIDTH, v=(index // WIDTH + 0.5) / HEIGHT)
        for layer, color in values.items():
            layers.setdefault(layer, bytearray()).extend(round(max(0.0, min(1.0, value)) * 255) for value in color)
    for layer, pixels in layers.items():
        write_png(name=f'{prefix}_{layer}.png', width=WIDTH, height=HEIGHT, pixels=bytes(pixels))


def main() -> None:
    OUTPUT.mkdir(exist_ok=True)
    generate(prefix='ceramic', sample=ceramic)
    generate(prefix='copper', sample=copper)
    generate(prefix='lava', sample=lava)
    generate(prefix='uv', sample=uv_grid)
    write_png(name='black.png', width=1, height=1, pixels=bytes(3))
    print(f'Textures written to {OUTPUT}')


if __name__ == '__main__':
    main()
