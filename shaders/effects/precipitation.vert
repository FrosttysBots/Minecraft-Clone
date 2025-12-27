#version 460 core
layout (location = 0) in vec3 aPos;      // Particle position
layout (location = 1) in float aSize;    // Particle size
layout (location = 2) in float aAlpha;   // Particle alpha

out float vAlpha;
out float vSize;
out vec2 vScreenPos;

uniform mat4 view;
uniform mat4 projection;
uniform float time;
uniform int weatherType;  // 1 = rain, 2 = snow, 3 = thunderstorm

void main() {
    vec3 pos = aPos;

    // Animation based on weather type
    if (weatherType == 2) {
        // Snow - gentle swaying motion
        float sway = sin(time * 0.8 + pos.x * 0.5) * 0.3 +
                     cos(time * 0.6 + pos.z * 0.4) * 0.2;
        pos.x += sway;
        pos.z += cos(time * 0.5 + pos.x * 0.3) * 0.15;
    }

    vec4 viewPos = view * vec4(pos, 1.0);
    gl_Position = projection * viewPos;

    // Size attenuation based on distance
    float dist = length(viewPos.xyz);
    float sizeScale = 300.0 / max(dist, 1.0);
    gl_PointSize = aSize * sizeScale;

    vAlpha = aAlpha;
    vSize = aSize;
    vScreenPos = gl_Position.xy / gl_Position.w;
}
