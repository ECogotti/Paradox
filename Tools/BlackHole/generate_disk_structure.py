"""Generate periodic linear RGBA flow/structure data. Requires numpy and Pillow."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image


def generate(output, seed=834722):
    width, height = 1024, 512
    rng = np.random.default_rng(seed)
    u, v = np.meshgrid(np.arange(width) / width, np.arange(height) / height)
    kx = np.fft.rfftfreq(width) * width
    ky = np.fft.fftfreq(height)[:, None] * height

    def field(bx, by):
        spectrum = np.fft.rfft2(rng.normal(size=(height, width)))
        spectrum *= np.exp(-.5 * ((kx / bx)**2 + (ky / by)**2))
        spectrum[0, 0] = 0
        result = np.fft.irfft2(spectrum, s=(height, width))
        return result / result.std()

    def wrap(x):
        return (x + .5) % 1 - .5

    macro = .12 * field(2, 3)
    potential = .10 * field(2, 2)
    filaments = np.zeros((height, width))
    for index in range(10):
        cu, cv = rng.uniform(0, 1, 2)
        su, sv = rng.uniform(.035, .10), rng.uniform(.035, .075)
        blob = np.exp(-.5 * ((wrap(u-cu) / su)**2 + (wrap(v-cv) / sv)**2))
        macro += rng.uniform(.6, 1.2) * blob
        potential += (-1 if index % 2 else 1) * .18 * blob
        for strand in range(3):
            phase = rng.uniform(0, 2*np.pi)
            center = cv + .018 * (strand-1) + .013 * np.sin(2*np.pi*(2*u)+phase)
            center += .008 * np.sin(2*np.pi*5*u + phase)
            envelope = np.exp(-.5*(wrap(u-cu) / rng.uniform(.10, .20))**2)
            ridge = np.exp(-.5*(wrap(v-center) / rng.uniform(.004, .009))**2)
            breaks = .35 + .65 * (.5+.5*np.sin(2*np.pi*3*u+phase))**2
            filaments += envelope * ridge * breaks * rng.uniform(.7, 1.1)

    # Curl of a periodic potential gives a smooth vector field with rotating cells.
    spectrum = np.fft.rfft2(potential)
    flow_u = np.fft.irfft2(spectrum * (2j*np.pi*ky), s=(height, width))
    flow_v = np.fft.irfft2(spectrum * (-2j*np.pi*kx), s=(height, width))
    flow_scale = max(np.max(np.sqrt(flow_u**2 + flow_v**2)), 1e-6)
    flow_u, flow_v = flow_u / flow_scale, flow_v / flow_scale
    clumps = np.tanh((macro-macro.mean()) / max(macro.std(), 1e-6) * .85)
    clumps -= clumps.mean()
    clumps /= max(np.max(np.abs(clumps)), 1e-6)
    clumps = .5 + .47 * clumps
    strands = np.clip(filaments, 0, 1)
    pixels = np.rint(np.stack((.5+.5*flow_u, .5+.5*flow_v, strands, clumps), axis=-1)*255).astype(np.uint8)
    output.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(pixels).save(output)
    report = {'seed': seed, 'size': [width, height], 'linear_data': True,
              'sha256': hashlib.sha256(output.read_bytes()).hexdigest(), 'channels': {}}
    for i, name in enumerate(('FlowU', 'FlowV', 'Filaments', 'Clumps')):
        data = pixels[:, :, i].astype(float) / 255
        edge = np.concatenate((data[:, 0]-data[:, -1], data[0]-data[-1]))
        inside = np.concatenate((np.diff(data, axis=0).ravel(), np.diff(data, axis=1).ravel()))
        adjacent = np.concatenate((data[:, 1]-data[:, 0], data[:, -1]-data[:, -2],
                                   data[1]-data[0], data[-1]-data[-2]))
        edge_rms, inside_rms = np.sqrt(np.mean(edge**2)), np.sqrt(np.mean(inside**2))
        adjacent_rms = np.sqrt(np.mean(adjacent**2))
        assert edge_rms < 2 * adjacent_rms, (name, edge_rms, adjacent_rms)
        report['channels'][name] = {'mean': float(data.mean()), 'std': float(data.std()),
                                    'wrap_rms': float(edge_rms), 'interior_rms': float(inside_rms),
                                    'adjacent_border_rms': float(adjacent_rms)}
    # The mip footprint includes this conservative global Jacobian bound.
    jac = np.sqrt((np.gradient(flow_u, axis=1)*width)**2 + (np.gradient(flow_u, axis=0)*height)**2
                  + (np.gradient(flow_v, axis=1)*width)**2 + (np.gradient(flow_v, axis=0)*height)**2)
    report['flow_jacobian_bound'] = float(np.ceil(jac.max()))
    output.with_suffix('.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[2] / 'SourceArt/BlackHole/T_BH_DiskStructure.png')
    parser.add_argument('--seed', type=int, default=834722)
    args = parser.parse_args()
    generate(args.output, args.seed)
