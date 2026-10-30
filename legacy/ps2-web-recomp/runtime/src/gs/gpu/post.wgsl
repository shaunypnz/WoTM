// The optional second pass of the window output (PS2_AA / PS2_SHARPEN, F9 cycles them): the scaled frame is first
// drawn at the window's size into a texture by blit.wgsl, then this pass smooths its stair-stepped edges (FXAA 3.11,
// quality preset 12) and/or restores detail lost to the scaling (contrast-adaptive sharpening, after AMD's CAS).
// Frame dumps and the PS2 picture itself never go through it.

struct Post {
    inv_width: f32,
    inv_height: f32,
    fxaa: u32,
    sharpen: f32,
}

@group(0) @binding(0) var image: texture_2d<f32>;
@group(0) @binding(1) var bilinear_sampler: sampler;
@group(0) @binding(2) var<uniform> post: Post;

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

fn at(uv: vec2<f32>) -> vec3<f32> {
    return textureSampleLevel(image, bilinear_sampler, uv, 0.0).rgb;
}

fn px(uv: vec2<f32>, x: f32, y: f32) -> vec3<f32> {
    return at(uv + vec2<f32>(x * post.inv_width, y * post.inv_height));
}

fn luma(c: vec3<f32>) -> f32 {
    return dot(c, vec3<f32>(0.299, 0.587, 0.114));
}

// FXAA quality 12's search steps (in pixels) for step i.
fn step_size(i: i32) -> f32 {
    if (i < 5) {
        return 1.0;
    }
    if (i == 5) {
        return 1.5;
    }
    if (i < 10) {
        return 2.0;
    }
    if (i == 10) {
        return 4.0;
    }
    return 8.0;
}

fn fxaa(uv: vec2<f32>) -> vec3<f32> {
    let rgb_m = at(uv);
    let l_m = luma(rgb_m);
    let l_n = luma(px(uv, 0.0, -1.0));
    let l_s = luma(px(uv, 0.0, 1.0));
    let l_e = luma(px(uv, 1.0, 0.0));
    let l_w = luma(px(uv, -1.0, 0.0));
    let l_max = max(l_m, max(max(l_n, l_s), max(l_e, l_w)));
    let l_min = min(l_m, min(min(l_n, l_s), min(l_e, l_w)));
    let range = l_max - l_min;
    if (range < max(0.0312, l_max * 0.125)) {
        return rgb_m;
    }
    let l_nw = luma(px(uv, -1.0, -1.0));
    let l_ne = luma(px(uv, 1.0, -1.0));
    let l_sw = luma(px(uv, -1.0, 1.0));
    let l_se = luma(px(uv, 1.0, 1.0));

    let edge_h = abs(-2.0 * l_w + l_nw + l_sw) + 2.0 * abs(-2.0 * l_m + l_n + l_s) + abs(-2.0 * l_e + l_ne + l_se);
    let edge_v = abs(-2.0 * l_n + l_nw + l_ne) + 2.0 * abs(-2.0 * l_m + l_w + l_e) + abs(-2.0 * l_s + l_sw + l_se);
    let horizontal = edge_h >= edge_v;

    // Which side of the pixel the edge lies on, and the step across it.
    var l_a = l_w;
    var l_b = l_e;
    var step_len = post.inv_width;
    if (horizontal) {
        l_a = l_n;
        l_b = l_s;
        step_len = post.inv_height;
    }
    let grad_a = abs(l_a - l_m);
    let grad_b = abs(l_b - l_m);
    var l_edge = 0.5 * (l_b + l_m);
    var gradient = 0.25 * max(grad_a, grad_b);
    if (grad_a >= grad_b) {
        step_len = -step_len;
        l_edge = 0.5 * (l_a + l_m);
    }

    // Walk both ways along the edge until its luma changes, then shift the sample towards the nearer end.
    var along = vec2<f32>(post.inv_width, 0.0);
    var start = uv;
    if (horizontal) {
        start.y += step_len * 0.5;
    } else {
        along = vec2<f32>(0.0, post.inv_height);
        start.x += step_len * 0.5;
    }
    var p1 = start - along;
    var p2 = start + along;
    var d1 = luma(at(p1)) - l_edge;
    var d2 = luma(at(p2)) - l_edge;
    var done1 = abs(d1) >= gradient;
    var done2 = abs(d2) >= gradient;
    for (var i = 1; i < 12; i++) {
        if (done1 && done2) {
            break;
        }
        if (!done1) {
            p1 -= along * step_size(i);
            d1 = luma(at(p1)) - l_edge;
            done1 = abs(d1) >= gradient;
        }
        if (!done2) {
            p2 += along * step_size(i);
            d2 = luma(at(p2)) - l_edge;
            done2 = abs(d2) >= gradient;
        }
    }
    var dist1 = uv.x - p1.x;
    var dist2 = p2.x - uv.x;
    if (!horizontal) {
        dist1 = uv.y - p1.y;
        dist2 = p2.y - uv.y;
    }
    let nearer1 = dist1 < dist2;
    let span = dist1 + dist2;
    let below = l_m < l_edge;
    var right_side = (d2 < 0.0) != below;
    if (nearer1) {
        right_side = (d1 < 0.0) != below;
    }
    var offset = 0.0;
    if (right_side) {
        offset = 0.5 - min(dist1, dist2) / span;
    }

    // Sub-pixel aliasing (single-pixel specks) from the 3x3 average.
    let avg = (2.0 * (l_n + l_s + l_e + l_w) + l_nw + l_ne + l_sw + l_se) / 12.0;
    let sub = clamp(abs(avg - l_m) / range, 0.0, 1.0);
    let sub_b = (-2.0 * sub + 3.0) * sub * sub;
    offset = max(offset, sub_b * sub_b * 0.75);

    var final_uv = uv;
    if (horizontal) {
        final_uv.y += offset * step_len;
    } else {
        final_uv.x += offset * step_len;
    }
    return at(final_uv);
}

fn cas(uv: vec2<f32>, e: vec3<f32>) -> vec3<f32> {
    let b = px(uv, 0.0, -1.0);
    let d = px(uv, -1.0, 0.0);
    let f = px(uv, 1.0, 0.0);
    let h = px(uv, 0.0, 1.0);
    let mn = min(e, min(min(b, d), min(f, h)));
    let mx = max(e, max(max(b, d), max(f, h)));
    let amp = sqrt(clamp(min(mn, vec3<f32>(1.0) - mx) / max(mx, vec3<f32>(1e-5)), vec3<f32>(0.0), vec3<f32>(1.0)));
    let w = amp * (-1.0 / mix(8.0, 5.0, post.sharpen));
    return clamp(((b + d + f + h) * w + e) / (vec3<f32>(1.0) + 4.0 * w), vec3<f32>(0.0), vec3<f32>(1.0));
}

@fragment
fn fs(in: VertexOut) -> @location(0) vec4<f32> {
    var c = at(in.uv);
    if (post.fxaa != 0u) {
        c = fxaa(in.uv);
    }
    if (post.sharpen > 0.0) {
        c = cas(in.uv, c);
    }
    return vec4<f32>(c, 1.0);
}
