#version 100
// FALLBACK sea for scene 2, used only by the Vita port when the full
// sea_frag.glsl program fails to link on the device compiler (src/scene2.cxx
// logs that failure and swaps this in). No texture fetches, no cloud deck and
// no loops, so it cannot fail the way the full program might; it keeps the
// scene readable with a lit sea and a sun path.
precision mediump float;

varying highp vec3 vWorld;

uniform highp vec3 uEyePos;
uniform highp float uTime;
uniform highp vec3 uSunDir;
uniform vec3 uWaterColor;

void main() {
    vec3 V = normalize(uEyePos - vWorld);
    vec3 L = normalize(uSunDir);
    float chop = sin(vWorld.x * 0.33 + uTime * 1.7) * sin(vWorld.z * 0.41 - uTime * 2.1);
    vec3 N = normalize(vec3(chop * 0.06, 1.0, chop * 0.05));

    float diff = max(dot(N, L), 0.0);
    vec3 c = uWaterColor * (0.9 + 1.1 * diff) + vec3(0.020, 0.014, 0.030);

    vec3 H = normalize(L + V);
    c += vec3(1.0, 0.72, 0.42) * pow(max(dot(N, H), 0.0), 64.0) * 0.55;

    c = c / (c + vec3(1.0));
    gl_FragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}
