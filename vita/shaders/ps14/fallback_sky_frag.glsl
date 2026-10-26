#version 100
// FALLBACK sky for scene 1, used only by the Vita port when the full
// sky_frag.glsl program fails to link on the device compiler (src/scene1.cxx
// logs that failure and swaps this in). It exists so a shader rejection costs
// the detail, not the frame: the scene still paints a dusk gradient with a sun
// glow instead of a black screen with nothing to read.
precision mediump float;

varying highp vec3 vDir;

uniform highp vec3 uSunDir;
uniform vec3 uHorizonColor;
uniform vec3 uMidColor;
uniform vec3 uZenithColor;

void main() {
    vec3 d = normalize(vDir);
    float h = clamp(d.y, 0.0, 1.0);
    vec3 c = mix(uHorizonColor, uMidColor, smoothstep(0.0, 0.14, h));
    c = mix(c, uZenithColor, smoothstep(0.10, 0.38, h));
    float sa = max(dot(d, normalize(uSunDir)), 0.0);
    c += vec3(1.20, 0.50, 0.16) * pow(sa, 26.0) * 0.55;
    c += vec3(1.00, 0.42, 0.14) * pow(sa, 90.0) * 0.45;
    c = c / (c + vec3(1.0));
    gl_FragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}
