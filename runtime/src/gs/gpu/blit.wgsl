struct Blit {
    width: u32,
    height: u32,
    current: u32,
    previous: u32,
    blend: u32,
    out_width: u32,
    out_height: u32,
    pad: u32,
}

@group(0) @binding(0) var<storage, read> pixels: array<u32>;
@group(0) @binding(1) var<uniform> blit: Blit;

struct VertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs(@builtin(vertex_index) i: u32) -> VertexOut {
    let p = vec2<f32>(f32((i << 1u) & 2u), f32(i & 2u));
    var o: VertexOut;
    o.position = vec4<f32>(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = vec2<f32>(p.x, 1.0 - p.y);
    return o;
}

fn texel(base: u32, x: i32, y: i32) -> vec4<f32> {
    let cx = u32(clamp(x, 0, i32(blit.width) - 1));
    let cy = u32(clamp(y, 0, i32(blit.height) - 1));
    return unpack4x8unorm(pixels[base + cy * blit.width + cx]);
}

fn sample(base: u32, uv: vec2<f32>) -> vec4<f32> {
    let size = vec2<f32>(f32(blit.width), f32(blit.height));
    let scale = max(floor(vec2<f32>(f32(blit.out_width), f32(blit.out_height)) / size), vec2<f32>(1.0));
    let t = uv * size;
    let d = fract(t) - 0.5;
    let region = 0.5 - 0.5 / scale;
    let p = floor(t) + (d - clamp(d, -region, region)) * scale;
    let origin = floor(p);
    let f = p - origin;
    let x = i32(origin.x);
    let y = i32(origin.y);
    let top = mix(texel(base, x, y), texel(base, x + 1, y), f.x);
    let bottom = mix(texel(base, x, y + 1), texel(base, x + 1, y + 1), f.x);
    return mix(top, bottom, f.y);
}

// A frame rendered above the window's size (PS2_SCALE 3-4 in a small window) is averaged over each window pixel's
// footprint instead of read at one point: a grid of bilinear taps (each already 2x2 texels) up to 4x4, so the
// extra resolution becomes anti-aliasing rather than shimmer. At or below the window's size this is `sample`.
fn resample(base: u32, uv: vec2<f32>) -> vec4<f32> {
    let size = vec2<f32>(f32(blit.width), f32(blit.height));
    let out = vec2<f32>(f32(blit.out_width), f32(blit.out_height));
    let ratio = size / out;
    if (ratio.x <= 2.0 && ratio.y <= 2.0) {
        return sample(base, uv);
    }
    let n = vec2<i32>(clamp(ceil(ratio * 0.5), vec2<f32>(1.0), vec2<f32>(4.0)));
    let step = 1.0 / (out * vec2<f32>(n));
    let start = uv - 0.5 / out + step * 0.5;
    var acc = vec4<f32>(0.0);
    for (var j = 0; j < n.y; j++) {
        for (var i = 0; i < n.x; i++) {
            acc += sample(base, start + step * vec2<f32>(f32(i), f32(j)));
        }
    }
    return acc / f32(n.x * n.y);
}

@fragment
fn fs(in: VertexOut) -> @location(0) vec4<f32> {
    var c = resample(blit.current, in.uv);
    if (blit.blend != 0u) {
        c = (c + resample(blit.previous, in.uv)) * 0.5;
    }
    return vec4<f32>(c.rgb, 1.0);
}
